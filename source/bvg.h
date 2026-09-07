#ifndef BVG_BVG_H
#define BVG_BVG_H

#include <stdint.h>
#include <time.h>

#define BVG_MAX_STOPS 5
#define BVG_MAX_JOURNEYS 4
#define BVG_LEG_MAX 8

typedef struct
{
	char id[64];
	char name[64];
	double lat;
	double lon;
} BvgStop;

typedef struct
{
	int depH, depM;
	int arrH, arrM;
	int walking;
	char label[16];
	char from[48];
	char to[48];
	char direction[48];
} BvgLeg;

typedef struct
{
	int depH, depM;
	int arrH, arrM;
	int durationMin;
	int transfers;
	char line[16];
	char direction[64];
	int legCount;
	BvgLeg legs[BVG_LEG_MAX];
} BvgJourney;

/* Search for a stop by name. Returns 0 on success and fills up to 'max'
   matches (sorted best first).
   -1 network/server error (details via status_out/err_out), -2 parse.
   status_out receives the HTTP status (0 if server never answered);
   err_out receives the first failing libctru Result code. */
int bvg_locations(const char* query, BvgStop* out, int max, int* count,
		  uint32_t* status_out, uint32_t* err_out);

/* Routes from stop to stop, departing at 'depart' (unix seconds).
   Returns 0 on success and fills up to 'max' journeys. Error codes as above. */
int bvg_journeys(const BvgStop* from, const BvgStop* to, time_t depart,
		 BvgJourney* out, int max, int* count,
		 uint32_t* status_out, uint32_t* err_out);

#endif