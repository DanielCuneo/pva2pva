
#include <dbAccess.h>

#include <dbEvent.h>
#include <dbLock.h>

#include <pv/pvIntrospect.h>
#include <pv/pvAccess.h>
#include <pv/json.h>

#define epicsExportSharedSymbols
#include "pdbgroup.h"

namespace {

namespace pvd = epics::pvData;

typedef std::map<std::string, AnyScalar> options_t;
typedef std::map<std::string, options_t> config_t;

struct context {
    std::string msg;
    std::string group, field, key;

    GroupConfig conf;

    void can_assign()
    {
        if(group.empty() || field.empty())
            throw std::runtime_error("Can't assign value in this context");
    }

    void assign(const AnyScalar& value) {
        can_assign();
        GroupConfig::Group& grp = conf.groups[group];

        if(key.empty()) {
            if(field=="+atomic") {
                grp.atomic = value.as<pvd::boolean>();
                grp.atomic_set = true;

            } else {
                conf.warning += "Unknown group option ";
                conf.warning += field;
            }
            field.clear();

        } else {
            GroupConfig::Field& fld = grp.fields[field];

            if(key=="+type") {
                fld.type = value.ref<std::string>();
                if(fld.type.empty())
                    throw std::runtime_error("+type can't be empty string");

            } else if(key=="+channel") {
                fld.channel = value.ref<std::string>();
                if(fld.channel.empty())
                    throw std::runtime_error("+channel can't be empty string");

            } else if(key=="+trigger") {
                fld.trigger = value.ref<std::string>();
                if(fld.trigger.empty())
                    throw std::runtime_error("+trigger can't be empty string");

            } else if(key=="+putorder") {
                fld.putorder = value.as<pvd::int32>();

            } else {
                conf.warning += "Unknown group field option ";
                conf.warning += field;
            }
            key.clear();
        }
    }
};

#define TRY context *self = (context*)ctx; try

#define CATCH() catch(std::exception& e) { if(self->msg.empty()) self->msg = e.what(); return 0; }

int conf_null(void * ctx)
{
    TRY {
        self->assign(AnyScalar());
        return 1;
    }CATCH()
}


int conf_boolean(void * ctx, int boolVal)
{
    TRY {
        self->assign(AnyScalar(pvd::boolean(boolVal)));
        return 1;
    }CATCH()
}

int conf_integer(void * ctx, long integerVal)
{
    TRY {
        self->assign(AnyScalar(pvd::int64(integerVal)));
        return 1;
    }CATCH()
}

int conf_double(void * ctx, double doubleVal)
{
    TRY {
        self->assign(AnyScalar(doubleVal));
        return 1;
    }CATCH()
}

int conf_string(void * ctx, const unsigned char * stringVal,
                    unsigned int stringLen)
{
    TRY {
        std::string val((const char*)stringVal, stringLen);
        self->assign(AnyScalar(val));
        return 1;
    }CATCH()
}

int conf_start_map(void * ctx)
{
    TRY {
        if(!self->group.empty() && !self->field.empty() && !self->key.empty())
            throw std::runtime_error("Too deep");
        return 1;
    }CATCH()
}

int conf_map_key(void * ctx, const unsigned char * key,
                     unsigned int stringLen)
{
    TRY {
        if(stringLen==0)
            throw std::runtime_error("empty key");

        std::string name((const char*)key, stringLen);

        if(self->group.empty())
            self->group.swap(name);
        else if(self->field.empty())
            self->field.swap(name);
        else if(self->key.empty())
            self->key.swap(name);
        else
            throw std::logic_error("Too deep!!");

        return 1;
    }CATCH()
}

int conf_end_map(void * ctx)
{
    TRY {
        assert(self->key.empty()); // cleared in assign()

        if(!self->field.empty())
            self->field.clear();
        else if(!self->group.empty())
            self->group.clear();

        return 1;
    }CATCH()
}

yajl_callbacks conf_cbs = {
    &conf_null,
    &conf_boolean,
    &conf_integer,
    &conf_double,
    NULL, // number
    &conf_string,
    &conf_start_map,
    &conf_map_key,
    &conf_end_map,
    NULL, // start_array,
    NULL, // end_array,
};

struct handler {
    yajl_handle handle;
    handler(yajl_handle handle) :handle(handle)
    {
        if(!handle)
            throw std::runtime_error("Failed to allocate yajl handle");
    }
    ~handler() {
        yajl_free(handle);
    }
    operator yajl_handle() { return handle; }
};

}// namespace

void GroupConfig::parse(const char *txt,
                        GroupConfig& result)
{
    yajl_parser_config conf;
    memset(&conf, 0, sizeof(conf));
    conf.allowComments = 1;
    conf.checkUTF8 = 1;

    context ctxt;

    handler handle(yajl_alloc(&conf_cbs, &conf, NULL, &ctxt));

    yajl_status sts = yajl_parse(handle, (const unsigned char*)txt, strlen(txt));

    if(sts==yajl_status_insufficient_data)
        sts = yajl_parse_complete(handle);

    switch(sts) {
    case yajl_status_ok: {
        size_t consumed = yajl_get_bytes_consumed(handle);
        if(consumed<strlen(txt)) {
            std::string leftovers(txt+consumed);
            if(leftovers.find_first_not_of(" \t\n\r")!=leftovers.npos)
                throw std::runtime_error("Trailing junk after json");
        }
        break;
    }
    case yajl_status_client_canceled:
        throw std::runtime_error(ctxt.msg);

    case yajl_status_insufficient_data:
        throw std::runtime_error("Unexpected end of input");

    case yajl_status_error:
    {
        unsigned char *raw = yajl_get_error(handle, 1, (const unsigned char*)txt, strlen(txt));
        try {
            std::string msg((char*)raw);
            yajl_free_error(handle, raw);
            throw std::runtime_error(msg);
        } catch(...){
            yajl_free_error(handle, raw);
            throw;
        }
    }
    }

    ctxt.conf.swap(result);
}