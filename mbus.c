#if !HAVE_CONFIG_H

#include <stdlib.h>

#include <string.h>

#ifndef __USE_ISOC99 /* required for NAN */
#define DISABLE_ISOC99 1
#define __USE_ISOC99 1
#endif /* !defined(__USE_ISOC99) */
#include <math.h>
#if DISABLE_ISOC99
#undef DISABLE_ISOC99
#undef __USE_ISOC99
#endif /* DISABLE_ISOC99 */

#include <time.h>
#define FP_LAYOUT_NEED_NOTHING 1
#define HAVE__BOOL 1
#endif /* ! HAVE_CONFIG */


#include <inttypes.h>

#include <collectd/core/daemon/collectd.h>

#include <collectd/core/daemon/common.h>
#include <collectd/core/daemon/plugin.h>

//##include "collectd.h"
//##include "common.h"
//##include "plugin.h"

#include <pthread.h>

/* this is the standard lib, not the plugin!!! */
#include <mbus/mbus.h>
#include <mbus/mbus-protocol-aux.h>

#include "mbus_utils.h"

static pthread_mutex_t plugin_lock; /**< Global lock fof the MBus access */

/* Note that the MBus library is not thread safe. On the other hand given the MBus */
/* nature - bus, synchronous communication (i.e. we can have only one operation in */
/* progress and have ot wait till it is finished) this is not a problem here for us. */
 
/* The only problem would be should we want to have multiple instances of MBus but */
/* this plugin represents only a single bus and collectd does not support multiple */
/* instances of a single plugin. */

static _Bool conf_is_serial = -1;
static char *conf_device    = NULL;
static char *conf_host      = NULL;
static int   conf_port      = 0;

static mbus_handle *handle = NULL; /**< MBus gateway */
static mbus_slave  *mbus_slaves;   /**< List of configured slaves */

/* =============================== CONFIGURATION ================================= */

/** 
 * Configure single MBus slave (i.e. process <Slave ..> element
 * 
 * @param ci Configuration to process
 * 
 * @return Zero when successful
 */
static int collectd_mbus_config_slave (oconfig_item_t *ci)
{
    mbus_slave     *slave           = NULL;
    mbus_address   *address         = NULL;
    int             i;
    oconfig_item_t *child           = NULL;
    _Bool           ignore_selected = 1;
    int             record_number;

    if((slave = mbus_slave_new()) == NULL)
    {
        ERROR("mbus: collectd_mbus_config_slave - cannot allocate new slave");
        return -2;
    }

    address = &(slave->address);

    if (ci->values_num == 1)
    {
        if(ci->values[0].type == OCONFIG_TYPE_STRING)
        {
            address->is_primary = 0;
            address->secondary = NULL;
            if(!cf_util_get_string (ci, &(address->secondary)) && strlen(address->secondary)==16)
            {
                DEBUG("mbus: collectd_mbus_config_slave - slave with primary address %s", address->secondary);
            }
            else
            {
                ERROR("mbus: collectd_mbus_config_slave - missing or wrong secondary slave address");
                mbus_slave_free(slave);
                return -1;
            }
        }
        else
        {
            if(ci->values[0].type == OCONFIG_TYPE_NUMBER)
            {
                address->is_primary = 1;
                if(!cf_util_get_int (ci, &(address->primary)) && address->primary<=250 && address->primary>=1)
                {
                    DEBUG("mbus: collectd_mbus_config_slave - slave with primary address %d", address->primary);
                }
                else
                {
                    ERROR("mbus: collectd_mbus_config_slave - wrong primary slave address (%d)", address->primary);
                    mbus_slave_free(slave);
                    return -1;
                }
            }
            else
            {
                ERROR("mbus: collectd_mbus_config_slave - missing or wrong slave address");
                mbus_slave_free(slave);
                return -1;
            }
        }
    }
    else
    {
        ERROR("mbus: collectd_mbus_config_slave - missing or wrong slave address");
        mbus_slave_free(slave);
        return -1;
    }
    
    /* first sort out the selection logic; last setting wins */
    for (i = 0; i < ci->children_num; i++)
    {
        child = ci->children + i;
        if ((strcasecmp ("IgnoreSelected", child->key) == 0) && (!cf_util_get_boolean (child, &ignore_selected)))
        {
            DEBUG("mbus: collectd_mbus_config_slave - IgnoreSelected = %d", ignore_selected);
        }
    }

    /* initialize the record mask array */
    mbus_slave_init_mask(slave, !ignore_selected);

    /* now set/clear the configured records */
    for (i = 0; i < ci->children_num; i++)
    {
        child = ci->children + i;

        if ((strcasecmp ("Record", child->key) == 0) && (!cf_util_get_int (child, &record_number)))
        {
            if(ignore_selected)
                mbus_slave_record_remove(slave, record_number);
            else
                mbus_slave_record_add(slave, record_number);
        }
    }

    slave->next_slave = mbus_slaves;
    mbus_slaves = slave;
    return 0;
}


/** 
 * Main plugin configuration callback
 * 
 * @param ci Top level configuration
 * 
 * @return Zero when successful.
 */
static int collectd_mbus_config (oconfig_item_t *ci)
{
    int i;
    int result = 0;
    
    DEBUG("==collectd_mbus_config==");

    pthread_mutex_lock (&plugin_lock);
    
    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;
        
        if ((strcasecmp ("IsSerial", child->key) == 0) && (!cf_util_get_boolean (child, &conf_is_serial)))
            ;
        else if ((strcasecmp ("SerialDevice", child->key) == 0) && (!cf_util_get_string (child, &conf_device)))
            ;
        else if ((strcasecmp ("Host", child->key) == 0) && (!cf_util_get_string (child, &conf_host)))
            ;
        else if ((strcasecmp ("Port", child->key) == 0) && (!cf_util_get_int (child, &conf_port)))
            ;
        else if ((strcasecmp ("Slave", child->key) == 0) && (!collectd_mbus_config_slave (child)))
            ;
        else
            WARNING ("mbus: collectd_mbus_config - unknown config option or unsupported config value: %s", child->key);
    }
    
    pthread_mutex_unlock(&plugin_lock);

    DEBUG("mbus: collectd_mbus_config - IsSerial = %d", conf_is_serial);

    if(conf_is_serial > 0)
    {
        if(conf_device)
            DEBUG("mbus: collectd_mbus_config - Device = %s", conf_device);
        else
        { 
            ERROR("mbus: collectd_mbus_config - Serial device not configured");
            result = -1;
        }
    }
    else if(conf_is_serial == 0)
    {
        if(conf_host)
            DEBUG("mbus: collectd_mbus_config - Host = %s", conf_host);
        else
        {
            ERROR("mbus: collectd_mbus_config - Host not configured");
            result = -1;
        }

        if(conf_port > 0)
            DEBUG("mbus: collectd_mbus_config - Port = %d", conf_port);
        else
        {
            ERROR("mbus: collectd_mbus_config - Port not configured");
            result = -1;
        }
    }
    else
    {
        ERROR("mbus: collectd_mbus_config - IsSerial not configured");
        result = -1;
    }
    
    return (result);
}




/* =============================== INIT ================================= */

/** 
 * Initialization callback
 * 
 * Only connects to the MBus gateway.
 * 
 * @return Zero when successful.
 */
static int collectd_mbus_init (void)
{
    DEBUG("mbus: collectd_mbus_init");

    pthread_mutex_lock (&plugin_lock);
    if(conf_is_serial)
    {
        if ((handle = mbus_context_serial(conf_device)) == NULL)
        {
            ERROR("mbus: collectd_mbus_init - Failed to setup serial connection to M-bus gateway");
            pthread_mutex_unlock (&plugin_lock);
            return -1;
        }
    }
    else
    {
        if ((handle = mbus_context_tcp(conf_host, conf_port)) == NULL)
        {
            ERROR("mbus: collectd_mbus_init - Failed to setup tcp connection to M-bus gateway");
            pthread_mutex_unlock (&plugin_lock);
            return -1;
        }
    }

    if(mbus_connect(handle))
    {
	    ERROR("mbus: collectd_mbus_init - Failed to connect");
    }

    mbus_serial_set_baudrate(handle, 9600);

    mbus_send_ping_frame(handle, MBUS_ADDRESS_NETWORK_LAYER, 1);
    mbus_send_ping_frame(handle, MBUS_ADDRESS_NETWORK_LAYER, 1);

    pthread_mutex_unlock (&plugin_lock);
    return (0);
}

/* =============================== SHUTDOWN ================================= */

/** 
 * Shutdown callback.
 * 
 * Disconnect the MBus gateway, delete it and delete all slaves.
 * 
 * @return Zero when successful (at the moment the only possible outcome)
 */
static int collectd_mbus_shutdown (void)
{
    mbus_slave * current_slave = mbus_slaves;
    mbus_slave * next_slave = NULL;

    DEBUG("mbus: collectd_mbus_shutdown");

    pthread_mutex_lock (&plugin_lock);
    if(handle != NULL)
        mbus_disconnect(handle);

    while(current_slave != NULL)
    {
        next_slave = current_slave->next_slave;
        mbus_slave_free(current_slave);
        current_slave = next_slave;
    }

    pthread_mutex_unlock (&plugin_lock);

    return (0);
}

/* =============================== READ ================================= */

/** 
 * Replace character in a null terminated string (in place).
 * 
 * Replaces all occurances of #search with #replace.
 * Helper function used to replace ' ' in value.type as types.db apparently
 * does not support it.
 *
 * @param string  Null terminated string in which we want to do the replacement.
 * @param search  Character we wnat to replace.
 * @param replace Character to be used as a replacement of #search
 */
static void str_char_replace(char * string, char search, char replace)
{
    while(*string != '\0')
    {
        if(*string == search)
            *string = replace;

        ++string;
    }
}


/** 
 * Parses MBus frame of a single slave and submits its data to the collectd framework.
 * 
 * @param slave MBus slave which frame we are processing.
 * @param frame Frame to process.
 * 
 * @return Zero when successful.
 */
static int parse_and_submit (mbus_slave * slave, mbus_frame * frame)
{
    value_list_t    vl = VALUE_LIST_INIT;
    mbus_frame_data frame_data;
    mbus_address   *address;

    DEBUG("mbus: parse_and_submit");
    
    if(slave == NULL)
    {
        ERROR("mbus: parse_and_submit - NULL slave");
        return -2;
    }
    address = &(slave->address);
    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "mbus", sizeof (vl.plugin));
    
    if(address->is_primary)
    {
        snprintf (vl.plugin_instance, sizeof (vl.plugin_instance), "%d", address->primary);
    }
    else
    {
        sstrncpy (vl.plugin_instance, 
                  address->secondary,
                  sizeof (vl.plugin_instance));
    }
    
    if(mbus_frame_data_parse(frame, &frame_data) == -1)
    {
        ERROR("mbus: parse_and_submit - failed mbus_frame_data_parse");
        return -1;
    }
    
    /*  ----------------------------- fixed structure ----------------------- */
    if (frame_data.type == MBUS_DATA_TYPE_FIXED)
    {
        char            *tmp_string;
        int              tmp_int;
        value_t          values[1];
        mbus_data_fixed *data;
        mbus_record     *record;

        DEBUG("mbus: parse_and_submit -   Fixed record");
        data = &(frame_data.data_fix);
        
        tmp_int = mbus_data_bcd_decode(data->id_bcd, 4);
        DEBUG("mbus: parse_and_submit -     Id            = %d", tmp_int);
        snprintf (vl.type, sizeof (vl.type), "%d", tmp_int);
        
        tmp_string = (char*) mbus_data_fixed_medium(data);
        DEBUG("mbus: parse_and_submit -     Medium        = %s", tmp_string);
        sstrncpy (vl.type_instance, tmp_string, sizeof (vl.type_instance));


        if(mbus_slave_record_check(slave,0))
        {
            DEBUG("mbus: parse_and_submit -   Record #0 enabled by mask");
            if(NULL != (record = mbus_parse_fixed_record(data->status, data->cnt1_type, data->cnt1_val)))
            {
                DEBUG("mbus: parse_and_submit -   Record #0");
                
                str_char_replace(record->quantity, ' ', '_');
                DEBUG("mbus: parse_and_submit -     Type            = %s", record->quantity);
                sstrncpy (vl.type, record->quantity, sizeof (vl.type));
                
                DEBUG("mbus: parse_and_submit -     Type instance   = 0");
                sstrncpy (vl.type_instance, "0", sizeof (vl.type_instance));

                if(record->quantity == NULL)
                {
                    WARNING("mbus: parse_and_submit - missing quantity for record #0");
                }
                else
                {
                    if(record->is_numeric)
                        values[0].gauge = record->value.real_val;
                    else
                        values[0].gauge = 0;
                    
                    DEBUG("mbus: parse_and_submit -     Value           = %lf", values[0].gauge);
                    
                    mbus_record_free(record);
                    record = NULL;
                    
                    vl.values_len = 1;
                    vl.values = values;
                    plugin_dispatch_values (&vl);
                }
            }
            else
            {
                ERROR("mbus: parse_and_submit - failed parsing fixed record");
                values[0].gauge = 0;
            }
        }
        else
        {
            DEBUG("mbus: parse_and_submit -   Record #0 disabled by mask");
        }
        
        if(mbus_slave_record_check(slave,1))
        {
            DEBUG("mbus: parse_and_submit -   Record #1 enabled by mask");
            if(NULL != (record = mbus_parse_fixed_record(data->status, data->cnt2_type, data->cnt2_val)))
            {
                DEBUG("mbus: parse_and_submit -   Record #1");
                
                str_char_replace(record->quantity, ' ', '_');
                DEBUG("mbus: parse_and_submit -     Type            = %s", record->quantity);
                sstrncpy (vl.type, record->quantity, sizeof (vl.type));
                
                DEBUG("mbus: parse_and_submit -     Type instance   = 1");
                sstrncpy (vl.type_instance, "1", sizeof (vl.type_instance));

                if(record->quantity == NULL)
                {
                    WARNING("mbus: parse_and_submit - missing quantity for record #0");
                }
                else
                {
                    if(record->is_numeric)
                        values[0].gauge = record->value.real_val;
                    else
                        values[0].gauge = 0;
                
                    DEBUG("mbus: parse_and_submit -     Value           = %lf", values[0].gauge);
                    
                    mbus_record_free(record);
                    record = NULL;
                    
                    vl.values_len = 1;
                    vl.values = values;
                    plugin_dispatch_values (&vl);
                }
            }
            else
            {
                ERROR("mbus: parse_and_submit - failed parsing fixed record");
            }
        }
        else
        {
            DEBUG("mbus: parse_and_submit -   Record #1 disabled by mask");

        } 
    }
    else
    {
        /* -------------------------- variable structure ---------------------- */
        if (frame_data.type == MBUS_DATA_TYPE_VARIABLE)
        {
            mbus_data_variable        *data;
            //mbus_data_variable_header *header;
            mbus_data_record          *data_record;
            size_t                     i;
            mbus_record               *record;
            value_t                    values[1];

            DEBUG("mbus: parse_and_submit -   Variable record");
            data = &(frame_data.data_var);
            //header = &(data->header);

            for (data_record = data->record, i = 0; 
                 data_record != NULL; 
                 data_record = (mbus_data_record *) (data_record->next), i++)
            {
                if(mbus_slave_record_check(slave,i))
                {
                    DEBUG("mbus: parse_and_submit -   Record #%d enabled by mask", i);
                    
                    if(NULL != (record = mbus_parse_variable_record(data_record)))
                    {
                        if(record->quantity == NULL)
                        {
                            WARNING("mbus: parse_and_submit - missing quantity for record #%d", i);
                            mbus_record_free(record);
                            record = NULL;
                            continue;
                        }
                        
                        DEBUG("mbus: parse_and_submit -   Record %d", i);
                        
                        str_char_replace(record->quantity, ' ', '_');
                        DEBUG("mbus: parse_and_submit -     Type            = %s", record->quantity);
                        sstrncpy (vl.type, record->quantity, sizeof (vl.type));
                        
                        DEBUG("mbus: parse_and_submit -     Type instance   = %d", i);
                        snprintf (vl.type_instance, sizeof (vl.type_instance), "%d", i);
                        
                        if(record->is_numeric)
                            values[0].gauge = record->value.real_val;
                        else
                            values[0].gauge = 0;
                        
                        DEBUG("mbus: parse_and_submit -     Value           = %lf", values[0].gauge);
                        
                        mbus_record_free(record);
                        record = NULL;
                        
                        vl.values_len = 1;
                        vl.values = values;
                        plugin_dispatch_values (&vl);
                    }
                    else
                    {
                        ERROR("mbus: parse_and_submit - failed parsing variable record");
                    }
                }
                else
                {
                    DEBUG("mbus: parse_and_submit -   Record #%d disabled by mask", i);
                }
            }
        }
    }
    
    if(frame_data.data_var.record)
    {
        mbus_data_record_free(frame_data.data_var.record);
    }
    
    return 0;
}


/** 
 * Plugin read callback.
 * 
 * @return Zero when successful.
 */
static int collectd_mbus_read (void)
{
    mbus_frame reply;
    mbus_slave * current_slave;


    DEBUG("mbus: collectd_mbus_read");
    pthread_mutex_lock (&plugin_lock);

    for(current_slave = mbus_slaves; current_slave != NULL; current_slave = current_slave->next_slave)
    {
        if(mbus_read_slave(handle, &(current_slave->address), &reply))
        {
            if(current_slave->address.is_primary)
            {
                ERROR("mbus: collectd_mbus_read - problem reading slave at primary address %d",
                      current_slave->address.primary);
            }
            else
            {
                ERROR("mbus: collectd_mbus_read - problem reading slave at secondary address %s",
                      current_slave->address.secondary);
            }
            continue;
        }

        parse_and_submit(current_slave, &reply);
    }

    pthread_mutex_unlock (&plugin_lock);
    return (0);
}



/* =============================== REGISTER ================================= */

/** 
 * PLugin "entry" - register all callback.
 * 
 */
void module_register (void)
{
    plugin_register_complex_config ("mbus", collectd_mbus_config);
    plugin_register_init ("mbus", collectd_mbus_init);
    plugin_register_shutdown ("mbus", collectd_mbus_shutdown);
    plugin_register_read ("mbus", collectd_mbus_read);
}
