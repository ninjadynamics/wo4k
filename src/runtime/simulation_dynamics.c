/* Experimental timestep-aware copy of the supplied PC dynamics reference.
 * Kept out of the playable adapter until native-equivalence and scaling tests pass.
 * dt=1 retains the native30Hz expressions exactly; dt=.5 integrates a60Hz step.
 */
#include "simulation_types.h"
static unsigned char *sim_image;
static double sim_dt=1;
static int sim_legacy=1;
static double sim_sqrt(double x) {double y;__asm__("fsqrt":"=t"(y):"0"(x));return y;}
typedef struct SimFractions {const ShipData *ship;double remainder[120];long stored[120];unsigned char valid[120];} SimFractions;
static SimFractions sim_fractions[32];
static SimFractions *sim_fraction_slot(const ShipData *ship) {
    unsigned int first=((unsigned int)ship>>4)&31;
    for(unsigned int n=0;n<32;++n) {
        SimFractions *slot=sim_fractions+((first+n)&31);
        if(slot->ship==ship)return slot;
        if(!slot->ship) {slot->ship=ship;return slot;}
    }
    SimFractions *slot=sim_fractions+first;slot->ship=ship;
    for(unsigned int n=0;n<120;++n)slot->valid[n]=0;
    return slot;
}
static double sim_exact(ShipData *ship,unsigned int offset,long current) {
    SimFractions *slot=sim_fraction_slot(ship);unsigned int key=offset/2;
    return current+(slot->valid[key] && slot->stored[key]==current?slot->remainder[key]:0);
}
static long sim_add(ShipData *ship,unsigned int offset,long current,double delta) {
    SimFractions *slot=sim_fraction_slot(ship);unsigned int key=offset/2;
    double value=sim_exact(ship,offset,current)+delta;long result=(long)value;
    slot->remainder[key]=value-result;slot->stored[key]=result;slot->valid[key]=1;
    return result;
}
#define SIM_ADD(s,f,d) ((s)->f=(__typeof__((s)->f))sim_add(s,offsetof(ShipData,f),(s)->f,(d)))
#define SIM_EXACT(s,f) sim_exact(s,offsetof(ShipData,f),(s)->f)
#define SIM_DECAY(s,f,d) SIM_ADD(s,f,SIM_EXACT(s,f)*((sim_dt==1?(d):sim_sqrt(d))-1))
static void sim_integrate_velocity(ShipData *ship,int friction) {
    double d=1-1.0/friction,r=sim_sqrt(d),gain=d/(1+r);
    SIM_ADD(ship,vpivot.vx,SIM_EXACT(ship,vpivot.vx)*(r-1)+ship->apivot.vx*gain);
    SIM_ADD(ship,vpivot.vy,SIM_EXACT(ship,vpivot.vy)*(r-1)+ship->apivot.vy*gain);
    SIM_ADD(ship,vpivot.vz,SIM_EXACT(ship,vpivot.vz)*(r-1)+ship->apivot.vz*gain);
}
#define TRACK_BASE 1
#define SPEED_UP 32
#define IN_TOW 1
#define DIRECTION 8
#define FLYING 16
#define LSIDE 32
#define RESCUE 0x400
#define track (*(TrackData **)(sim_image+0x65f794))
#define flyingFlags ((short *)(sim_image+0x8e670))
#define droidFlyin (*(int *)(sim_image+0x8f5b4))
#define GetMagnitude ((int (*)(float,float,float))(sim_image+0xc760))
#define PlaneLineCollision ((int (*)(VECTOR,VECTOR,SVector))(sim_image+0x17900))
#define CheckPolygonCollision ((int (*)(Face *,VECTOR,int,int))(sim_image+0x17ab0))
#define Ship2Track ((void (*)(ShipData *,Face *,Object **))(sim_image+0x163e0))
#define Ship2TrackBase ((void (*)(ShipData *,Face *,int))(sim_image+0x16230))
#define GetTargetPos ((void (*)(VECTOR *,VECTOR,Vector,Vector))(sim_image+0x3e090))
#define UpdateRescueDroid ((void (*)())(sim_image+0x4c780))
#define sar ((int (*)(int,int))(sim_image+0xc7c0))
#define ang ((short (*)(short))(sim_image+0xc7e0))
static void sim_track_reaction(register ShipData *playerShip, Object **shipShapes, short shipNo)
{
    VECTOR reaction;
    VECTOR targetVelocity;
    VECTOR nosePos;
    VECTOR facePoint;
    VECTOR targetPos;
    VECTOR toTarget;
    VECTOR direction;
    VECTOR nextDirection;
    short boost=1;
    short brake;
    int speed;
    float xVelocity;
    float yVelocity;
    float zVelocity;
    int clearance;
    int altitudeDifference;
    int friction;
    int xDrag;
    int yDrag;
    int zDrag;
    int mass;
    int alpha;
    Face *face;
    TrackSection *current;
    TrackSection *neighbour;
    TrackSection *next;

    if (playerShip->thrustCount == 0) {
        boost = 1;
    } else if (playerShip->thrustCount > 0) {
        if (playerShip->thrustCount < 3)
            boost = 3;
        else
            boost = 6;
    } else if (playerShip->thrustCount < 3) {
        /* The original leaves boost uninitialized for negative counts. */
    } else {
        boost = 6;
    }
    playerShip->thrustVector.vx = (playerShip->unitVecNose.vx * playerShip->thrust * boost) >> 6;
    playerShip->thrustVector.vy = (playerShip->unitVecNose.vy * playerShip->thrust * boost) >> 6;
    playerShip->thrustVector.vz = (playerShip->unitVecNose.vz * playerShip->thrust * boost) >> 6;

    xVelocity = playerShip->vpivot.vx >> 1;
    yVelocity = playerShip->vpivot.vy >> 1;
    zVelocity = playerShip->vpivot.vz >> 1;
    speed = GetMagnitude(xVelocity, yVelocity, zVelocity) << 1;
    if (playerShip->autopilotCount == 0) {
        playerShip->speed = speed >> 1;
    }
    targetVelocity.vx = (speed * playerShip->unitVecNose.vx) / 4096;
    targetVelocity.vy = (speed * playerShip->unitVecNose.vy) / 4096;
    targetVelocity.vz = (speed * playerShip->unitVecNose.vz) / 4096;

    if (playerShip->nearTrkSect->flags & 1) {
        face = track->faces + playerShip->nearTrkSect->faceStart;
        while (!(face->flags & TRACK_BASE)) {
            face++;
        }
        facePoint.vx = track->vertices[face->vertex[0]].vx;
        facePoint.vy = track->vertices[face->vertex[0]].vy;
        facePoint.vz = track->vertices[face->vertex[0]].vz;
        alpha = PlaneLineCollision(facePoint, playerShip->ppivot, face->normal);
        if (CheckPolygonCollision(face, playerShip->ppivot, alpha, 2) == 0) {
            playerShip->attr |= FLYING;
            flyingFlags[shipNo] |= 0x10;
        } else {
            flyingFlags[shipNo] = 0;
        }
    }

    if (!(playerShip->attr & FLYING)) {
        face = track->faces + playerShip->nearTrkSect->faceStart;
        while (!(face->flags & TRACK_BASE)) {
            face++;
        }
        Ship2Track(playerShip, face, shipShapes);
        if ((face->flags & SPEED_UP) && (playerShip->attr & LSIDE)) {
            playerShip->vpivot.vx += playerShip->nearTrkSect->nextSection.ptr->centre.vx - playerShip->nearTrkSect->centre.vx;
            playerShip->vpivot.vy += playerShip->nearTrkSect->nextSection.ptr->centre.vy - playerShip->nearTrkSect->centre.vy;
            playerShip->vpivot.vz += playerShip->nearTrkSect->nextSection.ptr->centre.vz - playerShip->nearTrkSect->centre.vz;
        }
        face++;
        if ((face->flags & SPEED_UP) && !(playerShip->attr & LSIDE)) {
            playerShip->vpivot.vx += playerShip->nearTrkSect->nextSection.ptr->centre.vx - playerShip->nearTrkSect->centre.vx;
            playerShip->vpivot.vy += playerShip->nearTrkSect->nextSection.ptr->centre.vy - playerShip->nearTrkSect->centre.vy;
            playerShip->vpivot.vz += playerShip->nearTrkSect->nextSection.ptr->centre.vz - playerShip->nearTrkSect->centre.vz;
        }
        face--;
        if (!(playerShip->attr & LSIDE)) {
            face++;
        }
        facePoint.vx = track->vertices[face->vertex[0]].vx;
        facePoint.vy = track->vertices[face->vertex[0]].vy;
        facePoint.vz = track->vertices[face->vertex[0]].vz;
        alpha = PlaneLineCollision(facePoint, playerShip->ppivot, face->normal);
        if (alpha < -300) {
            alpha = -300;
        }
        clearance = alpha;
        Ship2TrackBase(playerShip, face, clearance);

        brake = playerShip->r_brake_rot + playerShip->l_brake_rot;
        playerShip->apivot.vx = (targetVelocity.vx - playerShip->vpivot.vx) / (playerShip->skid + (brake >> 2));
        playerShip->apivot.vy = (targetVelocity.vy - playerShip->vpivot.vy) / (playerShip->skid + (brake >> 2));
        playerShip->apivot.vz = (targetVelocity.vz - playerShip->vpivot.vz) / (playerShip->skid + (brake >> 2));

        if (clearance < 75) {
            alpha = 75;
        }
        reaction.vx = (face->normal.vx << 14) / alpha;
        reaction.vy = (face->normal.vy << 14) / alpha;
        reaction.vz = (face->normal.vz << 14) / alpha;
        reaction.vx -= face->normal.vx << 6;
        reaction.vy -= face->normal.vy << 6;
        reaction.vz -= face->normal.vz << 6;
        reaction.vy += 30000;

        mass = playerShip->mass;
        altitudeDifference = playerShip->nearTrkSect->centre.vy - playerShip->ppivot.vy;
        if (altitudeDifference > 704) {
            if (mass == 110) {
                if(sim_dt==1) playerShip->ppivot.vy += 80; else SIM_ADD(playerShip,ppivot.vy,(80)*sim_dt);
            }
            if (playerShip->vpivot.vy < 0) {
                if(sim_dt==1) playerShip->vpivot.vy >>= 1; else SIM_DECAY(playerShip,vpivot.vy,.5);
            }
            if(sim_dt==1) playerShip->ppivot.vy += 16; else SIM_ADD(playerShip,ppivot.vy,(16)*sim_dt);
        }
        reaction.vy += altitudeDifference << 6;

        playerShip->apivot.vx += (reaction.vx + playerShip->thrustVector.vx) / mass;
        playerShip->apivot.vy += (reaction.vy + playerShip->thrustVector.vy) / mass;
        playerShip->apivot.vz += (reaction.vz + playerShip->thrustVector.vz) / mass;

        if(sim_dt==1) {
        playerShip->vpivot.vx += playerShip->apivot.vx;
        playerShip->vpivot.vy += playerShip->apivot.vy;
        playerShip->vpivot.vz += playerShip->apivot.vz;
        friction = (playerShip->resistance * (74 - (brake >> 3))) >> 7;
        xDrag = playerShip->vpivot.vx;
        yDrag = playerShip->vpivot.vy;
        zDrag = playerShip->vpivot.vz;
        playerShip->vpivot.vx = xDrag - xDrag / friction;
        playerShip->vpivot.vy = yDrag - yDrag / friction;
        playerShip->vpivot.vz = zDrag - zDrag / friction;
        } else {
            friction = (playerShip->resistance * (74 - (brake >> 3))) >> 7;
            sim_integrate_velocity(playerShip,friction);
        }

        if(sim_dt==1) playerShip->ppivot.vx += sar(playerShip->vpivot.vx, 6); else SIM_ADD(playerShip,ppivot.vx,((playerShip->vpivot.vx / 64.0))*sim_dt);
        if(sim_dt==1) playerShip->ppivot.vy += sar(playerShip->vpivot.vy, 6); else SIM_ADD(playerShip,ppivot.vy,((playerShip->vpivot.vy / 64.0))*sim_dt);
        if(sim_dt==1) playerShip->ppivot.vz += sar(playerShip->vpivot.vz, 6); else SIM_ADD(playerShip,ppivot.vz,((playerShip->vpivot.vz / 64.0))*sim_dt);

        nosePos.vx = (playerShip->unitVecNose.vx >> 5) + playerShip->ppivot.vx;
        nosePos.vy = (playerShip->unitVecNose.vy >> 5) + playerShip->ppivot.vy;
        nosePos.vz = (playerShip->unitVecNose.vz >> 5) + playerShip->ppivot.vz;
        alpha = PlaneLineCollision(facePoint, nosePos, face->normal);

        if (playerShip->noAttitudeControl == 0) {
            clearance -= alpha;
            if (alpha < 600) {
                if(sim_dt==1) playerShip->vpitch += clearance + 5; else SIM_ADD(playerShip,vpitch,(clearance + 5)*(sim_sqrt(.75)/(1+sim_sqrt(.75))));
            } else {
                if(sim_dt==1) playerShip->vpitch -= 50; else SIM_ADD(playerShip,vpitch,-(50)*(sim_sqrt(.75)/(1+sim_sqrt(.75))));
            }
            if(sim_dt==1) playerShip->vpitch -= playerShip->vpitch >> 2; else SIM_DECAY(playerShip,vpitch,.75);
            if(sim_dt==1) playerShip->hdg += ((short)((playerShip->r_brake_rot - playerShip->l_brake_rot) / 8) * playerShip->speed) / 32768; else SIM_ADD(playerShip,hdg,(((short)((playerShip->r_brake_rot - playerShip->l_brake_rot) / 8) * playerShip->speed) / 32768)*sim_dt);
            if(sim_dt==1) playerShip->vroll += (playerShip->vhdg / 32); else SIM_ADD(playerShip,vroll,((playerShip->vhdg / 32.0))*(sim_sqrt(.5)/(1+sim_sqrt(.5))));
            if(sim_dt==1) playerShip->vroll -= (playerShip->vroll >> 1); else SIM_DECAY(playerShip,vroll,.5);
        }
    } else {
        float targetDistanceX;
        float targetDistanceY;
        float targetDistanceZ;

        if (playerShip->vpivot.vy < playerShip->nearTrkSect->centre.vy) {
            face = track->faces + playerShip->nearTrkSect->faceStart;
            while (!(face->flags & TRACK_BASE)) {
                face++;
            }
            Ship2Track(playerShip, face, shipShapes);
        }
        neighbour = playerShip->nearTrkSect->nextSection.ptr;
        GetTargetPos(&targetPos, playerShip->ppivot, neighbour->centre, playerShip->nearTrkSect->centre);
        toTarget.vx = targetPos.vx - playerShip->ppivot.vx;
        toTarget.vy = targetPos.vy - playerShip->ppivot.vy;
        toTarget.vz = targetPos.vz - playerShip->ppivot.vz;
        if (toTarget.vy > 0) {
            toTarget.vy >>= 7;
        } else {
            toTarget.vx <<= 3;
            toTarget.vy <<= 3;
            toTarget.vz <<= 3;
        }
        targetDistanceX = toTarget.vx;
        targetDistanceY = toTarget.vy;
        targetDistanceZ = toTarget.vz;
        if (GetMagnitude(targetDistanceX, targetDistanceY, targetDistanceZ) > 32000) {
            playerShip->update = UpdateRescueDroid;
            playerShip->updateCount = 500;
            droidFlyin = 1000;
            playerShip->thrust = 0;
            playerShip->attr |= RESCUE | IN_TOW;
        }

        brake = playerShip->r_brake_rot + playerShip->l_brake_rot;
        playerShip->apivot.vx = (targetVelocity.vx - playerShip->vpivot.vx) / ((brake << 2) + 20);
        playerShip->apivot.vy = (targetVelocity.vy - playerShip->vpivot.vy) / ((brake << 2) + 20);
        playerShip->apivot.vz = (targetVelocity.vz - playerShip->vpivot.vz) / ((brake << 2) + 20);

        mass = playerShip->mass;
        altitudeDifference = playerShip->nearTrkSect->centre.vy - playerShip->ppivot.vy;
        if (altitudeDifference > 704) {
            if (mass == 110) {
                if(sim_dt==1) playerShip->ppivot.vy += 80; else SIM_ADD(playerShip,ppivot.vy,(80)*sim_dt);
            }
            if (playerShip->vpivot.vy < 0) {
                if(sim_dt==1) playerShip->vpivot.vy >>= 1; else SIM_DECAY(playerShip,vpivot.vy,.5);
            }
            if(sim_dt==1) playerShip->ppivot.vy += 16; else SIM_ADD(playerShip,ppivot.vy,(16)*sim_dt);
        }

        playerShip->apivot.vx += playerShip->thrustVector.vx / mass;
        playerShip->apivot.vy += (playerShip->thrustVector.vy + 80000) / mass;
        playerShip->apivot.vz += playerShip->thrustVector.vz / mass;

        if(sim_dt==1) {
        playerShip->vpivot.vx += playerShip->apivot.vx;
        playerShip->vpivot.vy += playerShip->apivot.vy;
        playerShip->vpivot.vz += playerShip->apivot.vz;
        friction = (playerShip->resistance * (74 - (brake >> 3))) >> 7;
        xDrag = playerShip->vpivot.vx;
        yDrag = playerShip->vpivot.vy;
        zDrag = playerShip->vpivot.vz;
        playerShip->vpivot.vx = xDrag - xDrag / friction;
        playerShip->vpivot.vy = yDrag - yDrag / friction;
        playerShip->vpivot.vz = zDrag - zDrag / friction;
        } else {
            friction = (playerShip->resistance * (74 - (brake >> 3))) >> 7;
            sim_integrate_velocity(playerShip,friction);
        }

        if(sim_dt==1) playerShip->ppivot.vx += (playerShip->vpivot.vx / 64); else SIM_ADD(playerShip,ppivot.vx,((playerShip->vpivot.vx / 64.0))*sim_dt);
        if(sim_dt==1) playerShip->ppivot.vy += (playerShip->vpivot.vy / 64); else SIM_ADD(playerShip,ppivot.vy,((playerShip->vpivot.vy / 64.0))*sim_dt);
        if(sim_dt==1) playerShip->ppivot.vz += (playerShip->vpivot.vz / 64); else SIM_ADD(playerShip,ppivot.vz,((playerShip->vpivot.vz / 64.0))*sim_dt);

        if(sim_dt==1) playerShip->vpitch -= 50; else SIM_ADD(playerShip,vpitch,-(50)*(sim_sqrt(.75)/(1+sim_sqrt(.75))));
        if(sim_dt==1) playerShip->vpitch -= playerShip->vpitch >> 2; else SIM_DECAY(playerShip,vpitch,.75);
        if(sim_dt==1) playerShip->hdg += ((short)((playerShip->r_brake_rot - playerShip->l_brake_rot) / 8) * playerShip->speed) / 32768; else SIM_ADD(playerShip,hdg,(((short)((playerShip->r_brake_rot - playerShip->l_brake_rot) / 8) * playerShip->speed) / 32768)*sim_dt);
        if(sim_dt==1) playerShip->vroll += (playerShip->vhdg / 32); else SIM_ADD(playerShip,vroll,((playerShip->vhdg / 32.0))*(sim_sqrt(.5)/(1+sim_sqrt(.5))));
        if(sim_dt==1) playerShip->vroll -= (playerShip->vroll / 2); else SIM_DECAY(playerShip,vroll,.5);
    }

    if(sim_dt==1) playerShip->hdg += (playerShip->vhdg / 64); else SIM_ADD(playerShip,hdg,((playerShip->vhdg / 64.0))*sim_dt);
    if(sim_dt==1) playerShip->pitch += playerShip->vpitch >> 4; else SIM_ADD(playerShip,pitch,((playerShip->vpitch / 16.0))*sim_dt);
    if(sim_dt==1) playerShip->roll += playerShip->vroll; else SIM_ADD(playerShip,roll,(playerShip->vroll)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
    if(sim_dt==1) playerShip->roll -= playerShip->roll >> 3; else SIM_DECAY(playerShip,roll,.875);
    playerShip->hdg = ang(playerShip->hdg);
    playerShip->pitch = ang(playerShip->pitch);
    playerShip->roll = ang(playerShip->roll);

    neighbour = playerShip->nearTrkSect->prevSection.ptr;
    current = playerShip->nearTrkSect;
    if (!(playerShip->attr & DIRECTION) && !(current->flags & 1) && (neighbour->flags & 1)) {
        if(sim_dt==1) playerShip->pitch -= playerShip->vpitch >> 4; else SIM_ADD(playerShip,pitch,-((playerShip->vpitch / 16.0))*sim_dt);
        if(sim_dt==1) playerShip->ppivot.vx -= (playerShip->vpivot.vx / 64); else SIM_ADD(playerShip,ppivot.vx,-((playerShip->vpivot.vx / 64.0))*sim_dt);
        if(sim_dt==1) playerShip->ppivot.vy -= (playerShip->vpivot.vy / 64); else SIM_ADD(playerShip,ppivot.vy,-((playerShip->vpivot.vy / 64.0))*sim_dt);
        if(sim_dt==1) playerShip->ppivot.vz -= (playerShip->vpivot.vz / 64); else SIM_ADD(playerShip,ppivot.vz,-((playerShip->vpivot.vz / 64.0))*sim_dt);
        direction.vx = playerShip->nearTrkSect->nextSection.ptr->centre.vx - playerShip->nearTrkSect->centre.vx;
        direction.vy = playerShip->nearTrkSect->nextSection.ptr->centre.vy - playerShip->nearTrkSect->centre.vy;
        direction.vz = playerShip->nearTrkSect->nextSection.ptr->centre.vz - playerShip->nearTrkSect->centre.vz;
        playerShip->vpivot.vx = direction.vx << 2;
        playerShip->vpivot.vy = direction.vy << 2;
        playerShip->vpivot.vz = direction.vz << 2;
        if(sim_dt==1) playerShip->ppivot.vx += (playerShip->vpivot.vx / 64); else SIM_ADD(playerShip,ppivot.vx,((playerShip->vpivot.vx / 64.0))*sim_dt);
        if(sim_dt==1) playerShip->ppivot.vy += (playerShip->vpivot.vy / 64); else SIM_ADD(playerShip,ppivot.vy,((playerShip->vpivot.vy / 64.0))*sim_dt);
        if(sim_dt==1) playerShip->ppivot.vz += (playerShip->vpivot.vz / 64); else SIM_ADD(playerShip,ppivot.vz,((playerShip->vpivot.vz / 64.0))*sim_dt);
    }

    next = playerShip->nearTrkSect->nextSection.ptr;
    nextDirection.vx = next->centre.vx - playerShip->nearTrkSect->centre.vx;
    nextDirection.vy = next->centre.vy - playerShip->nearTrkSect->centre.vy;
    nextDirection.vz = next->centre.vz - playerShip->nearTrkSect->centre.vz;
    if (nextDirection.vx * playerShip->unitVecNose.vx + nextDirection.vy * playerShip->unitVecNose.vy + nextDirection.vz * playerShip->unitVecNose.vz < 0 && playerShip->ppivot.vy >= playerShip->nearTrkSect->centre.vy + 1000 && (playerShip->attr & FLYING) && (current->flags & 1) && !(current->prevSection.ptr->flags & 1)) {
        playerShip->update = UpdateRescueDroid;
        playerShip->updateCount = 500;
        droidFlyin = 1000;
        playerShip->thrust = 0;
        playerShip->attr |= RESCUE | IN_TOW;
    }
}
