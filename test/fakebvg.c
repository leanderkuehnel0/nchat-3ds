#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "bvg.h"

int bvg_locations(const char* query, BvgStop* out, int max, int* count,
		  uint32_t* status_out, uint32_t* err_out)
{
	if (max < 1)
		return -1;
	memset(out, 0, sizeof(*out));
	snprintf(out->id, sizeof(out->id), "stop:%s", query);
	snprintf(out->name, sizeof(out->name), "%s", query);
	out->lat = 52.5;
	out->lon = 13.4;
	*count = 1;
	*status_out = 200;
	*err_out = 0;
	return 0;
}

static void leg(BvgLeg* l, int depH, int depM, int arrH, int arrM,
		int walking, const char* label, const char* from, const char* to,
		const char* dir)
{
	l->depH = depH; l->depM = depM;
	l->arrH = arrH; l->arrM = arrM;
	l->walking = walking;
	snprintf(l->label, sizeof(l->label), "%s", label);
	snprintf(l->from, sizeof(l->from), "%s", from);
	snprintf(l->to, sizeof(l->to), "%s", to);
	snprintf(l->direction, sizeof(l->direction), "%s", dir ? dir : "");
}

int bvg_journeys(const BvgStop* from, const BvgStop* to, time_t depart,
		 BvgJourney* out, int max, int* count,
		 uint32_t* status_out, uint32_t* err_out)
{
	(void)from; (void)to; (void)depart;

	memset(out, 0, sizeof(*out) * (size_t)max);

	out[0].depH = 12; out[0].depM = 30;
	out[0].arrH = 13; out[0].arrM = 15;
	out[0].durationMin = 45;
	out[0].transfers = 1;
	snprintf(out[0].line, sizeof(out[0].line), "U7");
	snprintf(out[0].direction, sizeof(out[0].direction),
		 "S+U Pankow (Berlin)");
	out[0].legCount = 2;
	leg(&out[0].legs[0], 12, 30, 12, 45, 0, "U7",
	    "U Mehringdamm (Berlin)", "M\u00F6ckernbr\u00FCcke (Berlin)",
	    "S+U Pankow (Berlin)");
	leg(&out[0].legs[1], 12, 52, 13, 15, 0, "U1",
	    "M\u00F6ckernbr\u00FCcke (Berlin)", "S+U Alexanderplatz/Dircksenstrasse (Berlin)",
	    "Warschauer Str. (Berlin)");

	out[1].depH = 12; out[1].depM = 32;
	out[1].arrH = 13; out[1].arrM = 22;
	out[1].durationMin = 50;
	out[1].transfers = 2;
	snprintf(out[1].line, sizeof(out[1].line), "U6+M8");
	snprintf(out[1].direction, sizeof(out[1].direction),
		 "Richtung Alt-Tegel (Berlin)");
	out[1].legCount = 3;
	leg(&out[1].legs[0], 12, 32, 12, 40, 0, "U6",
	    "U Mehringdamm (Berlin)", "Hallesches Tor (Berlin)",
	    "Richtung Alt-Tegel (Berlin)");
	leg(&out[1].legs[1], 12, 40, 12, 48, 1, "",
	    "Hallesches Tor (Berlin)", "Kochstra\u00DFe (Berlin)", NULL);
	leg(&out[1].legs[2], 12, 48, 13, 22, 0, "M8",
	    "Kochstra\u00DFe (Berlin)", "S+U Alexanderplatz (Berlin)",
	    "S+U Alexanderplatz/Dircksenstrasse (Berlin)");

	out[2].depH = 12; out[2].depM = 35;
	out[2].arrH = 13; out[2].arrM = 19;
	out[2].durationMin = 44;
	out[2].transfers = 0;
	snprintf(out[2].line, sizeof(out[2].line), "U7");
	snprintf(out[2].direction, sizeof(out[2].direction), "Rudow (Berlin)");
	out[2].legCount = 1;
	leg(&out[2].legs[0], 12, 35, 13, 19, 0, "U7",
	    "U Mehringdamm (Berlin)", "S+U Alexanderplatz/Dircksenstrasse (Berlin)",
	    "Rudow (Berlin)");

	*count = (max < 3) ? max : 3;
	*status_out = 200;
	*err_out = 0;
	return 0;
}