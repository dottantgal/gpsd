/****************************************************************************

NAME
   rtcm2_json.c - deserialize RTCM2 JSON

DESCRIPTION
   This module uses the generic JSON parser to get data from RTCM2
representations to libgps structures.

***************************************************************************/

#include <math.h>
#include <assert.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

#include "gpsd_config.h"
#include "gpsd.h"
#include "gps_json.h"

/* common fields in every RTCM2 message */

int json_rtcm2_read(const char *buf, 
		    char *path, size_t pathlen,
		    struct rtcm2_t *rtcm2, 
		    const char **endptr)
{

    static char *stringptrs[NITEMS(rtcm2->words)];
    static char stringstore[sizeof(rtcm2->words)*2];
    static int stringcount;

#define RTCM2_HEADER \
	{"class",          check,    .dflt.check = "RTCM2"}, \
	{"type",           uinteger, .addr.uinteger = &rtcm2->type}, \
	{"device",         string,   .addr.string = path, \
	                             .len = pathlen}, \
	{"station_id",     uinteger, .addr.uinteger = &rtcm2->refstaid}, \
	{"zcount",         real,     .addr.real = &rtcm2->zcount, \
			             .dflt.real = NAN}, \
	{"seqnum",         uinteger, .addr.uinteger = &rtcm2->seqnum}, \
	{"length",         uinteger, .addr.uinteger = &rtcm2->length}, \
	{"station_health", uinteger, .addr.uinteger = &rtcm2->stathlth},

    int status, satcount;

    const struct json_attr_t rtcm1_satellite[] = {
	{"ident",     uinteger, STRUCTOBJECT(struct rangesat_t, ident)},
	{"udre",      uinteger, STRUCTOBJECT(struct rangesat_t, udre)},
	{"issuedata", real,     STRUCTOBJECT(struct rangesat_t, issuedata)},
	{"rangerr",   real,     STRUCTOBJECT(struct rangesat_t, rangerr)},
	{"rangerate", real,     STRUCTOBJECT(struct rangesat_t, rangerate)},
	{NULL},
    };
    const struct json_attr_t json_rtcm1[] = {
	RTCM2_HEADER
        {"satellites", array,	STRUCTARRAY(rtcm2->ranges.sat, 
					    rtcm1_satellite, &satcount)},
	{NULL},
    };

    const struct json_attr_t json_rtcm3[] = {
	RTCM2_HEADER
        {"valid",          boolean, .addr.boolean = &rtcm2->reference.valid},
	{"x",              real,    .addr.real = &rtcm2->ecef.x,
			            .dflt.real = NAN},
	{"y",              real,    .addr.real = &rtcm2->ecef.y,
			            .dflt.real = NAN},
	{"z",              real,    .addr.real = &rtcm2->ecef.z,
			            .dflt.real = NAN},
	{NULL},
    };

    const struct json_attr_t json_rtcm4[] = {
	RTCM2_HEADER
        {"valid",          boolean, .addr.boolean = &rtcm2->reference.valid},
	{"system",         integer, .addr.integer = &rtcm2->reference.system},
	{"sense",          integer, .addr.integer = &rtcm2->reference.sense},
	{"datum",          string,  .addr.string = rtcm2->reference.datum,
	                            .len = sizeof(rtcm2->reference.datum)},
	{"dx",             real,    .addr.real = &rtcm2->reference.dx,
			            .dflt.real = NAN},
	{"dy",             real,    .addr.real = &rtcm2->reference.dy,
			            .dflt.real = NAN},
	{"dz",             real,    .addr.real = &rtcm2->reference.dz,
			            .dflt.real = NAN},
	{NULL},
    };

    const struct json_attr_t rtcm5_satellite[] = {
	{"ident",       uinteger, STRUCTOBJECT(struct consat_t, ident)},
	{"iodl",        boolean,  STRUCTOBJECT(struct consat_t, iodl)},
	{"health",      uinteger, STRUCTOBJECT(struct consat_t, health)},
	{"health_en",   boolean,  STRUCTOBJECT(struct consat_t, health_en)},
	{"new_data",    boolean,  STRUCTOBJECT(struct consat_t, new_data)},
	{"los_warning", boolean,  STRUCTOBJECT(struct consat_t, los_warning)},
	{"tou",         uinteger, STRUCTOBJECT(struct consat_t, tou)},
	{NULL},
    };
    const struct json_attr_t json_rtcm5[] = {
	RTCM2_HEADER
        {"satellites", array,	STRUCTARRAY(rtcm2->conhealth.sat, 
					    rtcm5_satellite, &satcount)},
	{NULL},
    };

    const struct json_attr_t json_rtcm6[] = {
	RTCM2_HEADER
	// No-op or keepalive message
	{NULL},
    };

    const struct json_attr_t rtcm7_satellite[] = {
	{"latitude",    real,     STRUCTOBJECT(struct station_t, latitude)},
	{"longitude",   real,     STRUCTOBJECT(struct station_t, longitude)},
	{"range",       uinteger, STRUCTOBJECT(struct station_t, range)},
	{"frequency",   real,     STRUCTOBJECT(struct station_t, frequency)},
	{"health",      uinteger, STRUCTOBJECT(struct station_t, health)},
	{"station_id",  uinteger, STRUCTOBJECT(struct station_t, station_id)},
	{"bitrate",     uinteger, STRUCTOBJECT(struct station_t, bitrate)},
	{NULL},
    };
    const struct json_attr_t json_rtcm7[] = {
	RTCM2_HEADER
        {"satellites", array,	STRUCTARRAY(rtcm2->almanac.station, 
					    rtcm7_satellite, &satcount)},
	{NULL},
    };

    const struct json_attr_t json_rtcm16[] = {
	RTCM2_HEADER
	{"message",        string,  .addr.string = rtcm2->message,
	                            .len = sizeof(rtcm2->message)},
	{NULL},
    };

    const struct json_attr_t json_rtcm2_fallback[] = {
	RTCM2_HEADER
	{"data",         array, .addr.array.element_type = string,
	                        .addr.array.arr.strings.ptrs = stringptrs,
	                        .addr.array.arr.strings.store = stringstore,
	                        .addr.array.arr.strings.storelen = sizeof(stringstore),
	                        .addr.array.count = &stringcount,
	                        .addr.array.maxlen = NITEMS(stringptrs)},
	{NULL},
    };

#undef STRUCTARRAY
#undef STRUCTOBJECT
#undef RTCM2_HEADER

    memset(rtcm2, '\0', sizeof(struct rtcm2_t));

    if (strstr(buf, "\"type\":1")!=NULL || strstr(buf, "\"type\":9")!=NULL) {
	status = json_read_object(buf, json_rtcm1, endptr);
	if (status == 0)
	    rtcm2->ranges.nentries = (unsigned)satcount;
    } else if (strstr(buf, "\"type\":3") != NULL)
	status = json_read_object(buf, json_rtcm3, endptr);
    else if (strstr(buf, "\"type\":4") != NULL) {
	status = json_read_object(buf, json_rtcm4, endptr);
    } else if (strstr(buf, "\"type\":5") != NULL)
	status = json_read_object(buf, json_rtcm5, endptr);
	if (status == 0)
	    rtcm2->conhealth.nentries = (unsigned)satcount;
    else if (strstr(buf, "\"type\":6") != NULL)
	status = json_read_object(buf, json_rtcm6, endptr);
    else if (strstr(buf, "\"type\":7") != NULL)
	status = json_read_object(buf, json_rtcm7, endptr);
	if (status == 0)
	    rtcm2->almanac.nentries = (unsigned)satcount;
    else if (strstr(buf, "\"type\":16") != NULL)
	status = json_read_object(buf, json_rtcm16, endptr);
    else {
	int n;
	status = json_read_object(buf, json_rtcm2_fallback, endptr);
	for (n = 0; n < NITEMS(rtcm2->words); n++)
	    if (n >= stringcount) {
		rtcm2->words[n] = 0;
	    } else {
		unsigned int u;
		int fldcount = sscanf(stringptrs[n], "U\t0x%08x\n", &u);
		if (fldcount != 1)
		return JSON_ERR_MISC;
	    else
		rtcm2->words[n] = (isgps30bits_t)u;
	}
	
    }
    if (status != 0)
	return status;
    return 0;
}

/* rtcm2_json.c ends here */