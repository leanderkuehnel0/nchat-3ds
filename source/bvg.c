#include "bvg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "web.h"

static void urlencode(const char* in, char* out, size_t outlen)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t o = 0;
	for (; *in && o + 3 < outlen; in++)
	{
		unsigned char c = (unsigned char)*in;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
		{
			out[o++] = (char)c;
		}
		else
		{
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 0xF];
		}
	}
	out[o] = '\0';
}

static const JsonNode* jv(const JsonNode* obj, const char* key)
{
	const JsonNode* n = json_object_get(obj, key);
	if (n && n->type == JSON_NULL)
		return NULL;
	return n;
}

static int parse_tod(const char* iso, int* h, int* m)
{
	if (!iso)
		return -1;
	const char* t = strchr(iso, 'T');
	if (!t)
		return -1;
	if (sscanf(t + 1, "%d:%d", h, m) != 2)
		return -1;
	return 0;
}

static void copy_str(char* dst, size_t dstsz, const char* src)
{
	if (!src || dstsz == 0)
		return;
	snprintf(dst, dstsz, "%.*s", (int)(dstsz - 1), src);
}

int bvg_locations(const char* query, BvgStop* out, int max, int* count,
		  u32* status_out, u32* err_out)
{
	*count = 0;
	if (status_out)
		*status_out = 0;
	if (err_out)
		*err_out = 0;
	if (max <= 0)
		return 0;

	char enc[256];
	urlencode(query, enc, sizeof(enc));

	char url[512];
	snprintf(url, sizeof(url),
		 "https://api.transitous.org/api/v1/geocode?text=%s&type=STOP"
		 "&numResults=%d&language=de",
		 enc, max);

	char* body = http_get(url, status_out, err_out);
	if (!body)
		return -1;

	JsonNode* root = json_parse(body);
	free(body);
	if (!root || root->type != JSON_ARRAY)
	{
		json_free(root);
		return -2;
	}

	int n = 0;
	int total = json_array_len(root);
	if (total > max)
		total = max;

	for (int i = 0; i < total && n < max; i++)
	{
		const JsonNode* m = json_array_at(root, i);
		if (!m || m->type != JSON_OBJECT)
			continue;
		const char* ty = json_string(jv(m, "type"));
		if (ty && strcmp(ty, "STOP") != 0)
			continue;
		const char* name = json_string(jv(m, "name"));
		if (!name)
			continue;

		out[n].lat = 0;
		out[n].lon = 0;
		json_number(jv(m, "lat"), &out[n].lat);
		json_number(jv(m, "lon"), &out[n].lon);
		copy_str(out[n].id, sizeof(out[n].id), json_string(jv(m, "id")));
		copy_str(out[n].name, sizeof(out[n].name), name);
		n++;
	}

	json_free(root);
	*count = n;
	return 0;
}

int bvg_journeys(const BvgStop* from, const BvgStop* to, time_t depart,
		 BvgJourney* out, int max, int* count,
		 u32* status_out, u32* err_out)
{
	*count = 0;
	if (status_out)
		*status_out = 0;
	if (err_out)
		*err_out = 0;
	if (max <= 0)
		return 0;

	char fp[48], tp[48];
	snprintf(fp, sizeof(fp), "%.6f,%.6f", from->lat, from->lon);
	snprintf(tp, sizeof(tp), "%.6f,%.6f", to->lat, to->lon);

	struct tm gtm;
	gmtime_r(&depart, &gtm);
	char depstr[32];
	strftime(depstr, sizeof(depstr), "%Y-%m-%dT%H:%M:%SZ", &gtm);

	char url[640];
	snprintf(url, sizeof(url),
		 "https://api.transitous.org/api/v6/plan?fromPlace=%s&toPlace=%s"
		 "&time=%s&arriveBy=false&numItineraries=%d"
		 "&timetableView=true&detailedLegs=false&detailedTransfers=false",
		 fp, tp, depstr, max);

	char* body = http_get(url, status_out, err_out);
	if (!body)
		return -1;

	JsonNode* root = json_parse(body);
	free(body);
	if (!root || root->type != JSON_OBJECT)
	{
		json_free(root);
		return -2;
	}

	const JsonNode* its = json_object_get(root, "itineraries");
	if (!its || its->type != JSON_ARRAY)
	{
		json_free(root);
		return -2;
	}

	int written = 0;
	int total = json_array_len(its);
	if (total > max)
		total = max;

	for (int i = 0; i < total; i++)
	{
		const JsonNode* it = json_array_at(its, i);
		if (!it || it->type != JSON_OBJECT)
			continue;

		BvgJourney j;
		memset(&j, 0, sizeof(j));

		double d = 0;
		json_number(jv(it, "duration"), &d);
		j.durationMin = (int)(d / 60.0 + 0.5);

		json_number(jv(it, "transfers"), &d);
		j.transfers = (int)d;

		parse_tod(json_string(jv(it, "startTime")), &j.depH, &j.depM);
		parse_tod(json_string(jv(it, "endTime")), &j.arrH, &j.arrM);

		const JsonNode* legs = json_object_get(it, "legs");
		if (!legs || legs->type != JSON_ARRAY)
			continue;

		int legCount = json_array_len(legs);
		if (legCount > BVG_LEG_MAX)
			legCount = BVG_LEG_MAX;

		int transitCount = 0;
		int firstTransit = -1;

		for (int k = 0; k < legCount; k++)
		{
			const JsonNode* leg = json_array_at(legs, k);
			if (!leg || leg->type != JSON_OBJECT)
				continue;

			const char* mode = json_string(jv(leg, "mode"));
			int walking = (mode && strcmp(mode, "WALK") == 0);

			const JsonNode* fromP = jv(leg, "from");
			const JsonNode* toP = jv(leg, "to");
			const char* fname = json_string(jv(fromP, "name"));
			const char* tname = json_string(jv(toP, "name"));
			if (!fname || strcmp(fname, "START") == 0)
				fname = (k == 0) ? from->name : NULL;
			if (!tname || strcmp(tname, "END") == 0)
				tname = (k == legCount - 1) ? to->name : NULL;

			BvgLeg* l = &j.legs[k];
			parse_tod(json_string(jv(leg, "startTime")), &l->depH, &l->depM);
			parse_tod(json_string(jv(leg, "endTime")), &l->arrH, &l->arrM);
			copy_str(l->from, sizeof(l->from), fname);
			copy_str(l->to, sizeof(l->to), tname);

			if (walking)
			{
				l->walking = 1;
				copy_str(l->label, sizeof(l->label), "walk");
			}
			else
			{
				l->walking = 0;
				transitCount++;
				const JsonNode* ln = jv(leg, "displayName");
				if (!ln)
					ln = jv(leg, "routeShortName");
				const char* lineName = json_string(ln);
				if (lineName)
					copy_str(l->label, sizeof(l->label), lineName);
				if (firstTransit < 0)
				{
					firstTransit = k;
					copy_str(j.line, sizeof(j.line), lineName);
				}
				const char* hsig = json_string(jv(leg, "headsign"));
				if (!hsig)
					hsig = tname;
				copy_str(l->direction, sizeof(l->direction), hsig);
			}
		}

		if (transitCount == 0)
			continue;

		j.legCount = legCount;

		const JsonNode* fl = json_array_at(legs, firstTransit);
		const char* dirn = json_string(jv(fl, "headsign"));
		if (!dirn)
			dirn = json_string(jv(jv(fl, "to"), "name"));
		copy_str(j.direction, sizeof(j.direction), dirn);

		out[written++] = j;
	}

	json_free(root);
	*count = written;
	return 0;
}