/* ABI definitions from the supplied PC reference; verified against this EXE. */
#ifndef WO_SIM_TYPES_H
#define WO_SIM_TYPES_H
#include <stddef.h>
typedef unsigned char u_char;typedef unsigned short u_short;
typedef struct {long vx,vy,vz,pad;} VECTOR;
typedef struct {long vx,vy,vz;} Vector;
typedef struct {short vx,vy,vz;} SVector;
typedef struct Object Object;
struct TrackSection;
typedef union SectionPtr {struct TrackSection *ptr;long index;} SectionPtr;
typedef struct Face {
    short   vertex[4];
    SVector normal;
    char    texture;
    u_char  flags;
    char    red;
    char    green;
    char    blue;
} Face;
typedef struct TrackSection {
    SectionPtr junction;
    SectionPtr prevSection;
    SectionPtr nextSection;
    Vector     centre;
    short      version;
    short      pad;
    short     *objects;
    short      objectCount;
    short     *northViewSection[3];
    short     *southViewSection[3];
    short     *eastViewSection[3];
    short     *westViewSection[3];
    short     *allViewSection[3];
    short      northViewCount[3];
    short      southViewCount[3];
    short      eastViewCount[3];
    short      westViewCount[3];
    short      allViewCount[3];
    short      high[4];
    short      med[4];
    short      faceStart;
    short      faceCount;
    short      globalRadius;
    short      localRadius;
    short      flags;
    short      secNo;
    signed char bump;
    char       snow;
} TrackSection;
typedef struct TrackData {
    long          vertexCount;
    long          faceCount;
    long          sectionCount;
    VECTOR       *vertices;
    Face         *faces;
    TrackSection *sections;
    short        *viewList;
    struct Colors *colCopy;
} TrackData;
typedef struct ShipData {
    short         highResTableIndex;
    short         lowResTableIndex;
    TrackSection *nearTrkSect;
    TrackSection *prevShipSect;
    int           attr;
    VECTOR        unitVecNose;
    VECTOR        unitVecWing;
    VECTOR        unitVecFin;
    VECTOR        ppivot;
    VECTOR        vpivot;
    VECTOR        apivot;
    short         hdg, pitch, roll;
    short         vhdg, vpitch, vroll;
    short         ahdg;
    short         mass;
    VECTOR        thrustVector;
    short         r_brake_rot;
    short         l_brake_rot;
    u_short       speed;
    short         reserved96;
    short         thrust;
    short         maxThrust;
    short         thrustCount;
    short         noAttitudeControl;
    short         headingInc;
    short         maxHeading;
    short         resistance;
    short         skid;
    short         lateralOffset;
    short         moveDist;
    u_short       combatAttr;
    short         boostCount;
    union {
        struct {
            short distance;
            short trackDistance;
        };
        struct {
            short distance;
            short trackDistance;
        } raceProgress;
    };
    u_short       remoteMagnitude;
    u_short       remoteMax;
    u_char        weapon;
    u_char        lastWeapon;
    u_char        rearFire;
    signed char   targetShip;
    signed char   rearTarget;
    u_char        padBD;
    short         electroCount;
    short         autopilotCount;
    short         stallCount;
    signed char   lap;
    signed char   reverseLap;
    short         nearSecNo;
    union {
        struct {
            short prevSecNo;
            short pad_CA;
            int lapTimes[5];
        };
        int raceLapTimes[6];
    };
    int           updateCount;
    int           reservedE4;
    int           startY;       /* hover height: ppivot.vy at race start, the ship bobs around it */
void (*update)();
} ShipData;
_Static_assert(sizeof(Face)==20,"Face ABI");
_Static_assert(sizeof(TrackSection)==156,"TrackSection ABI");
_Static_assert(sizeof(ShipData)==240,"Ship ABI");
_Static_assert(offsetof(ShipData,ppivot)==0x40,"Ship position ABI");
_Static_assert(offsetof(ShipData,update)==0xec,"Ship callback ABI");
#endif
