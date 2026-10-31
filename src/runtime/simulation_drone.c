/* Native drone control reference, before timestep conversion. */
#define ownShip (*(short *)(sim_image+0x664820))
#define shipClassIndex (*(short *)(sim_image+0x65f7c2))
#define clipLevel (*(short *)(sim_image+0x65f688))
#define activeShipCount (*(short *)(sim_image+0x66510a))
#define numTrackSections (*(short *)(sim_image+0x6650f0))
#define shipOrder ((short (*)[2])(sim_image+0x65f7e2))
#define classSpeedLimit ((short *)(sim_image+0x8f790))
#define classSpeedMargin ((short *)(sim_image+0x8f798))
#define classAccel ((short *)(sim_image+0x8f7a0))
#define classSteer ((short *)(sim_image+0x8f7a8))
#define pitLaneSection1 ((short *)(sim_image+0x8f7b0))
#define pitLaneSection2 ((short *)(sim_image+0x8f7c0))
#define pitLaneDirection ((int *)(sim_image+0x8f7d0))
#define shipDamage ((int *)(sim_image+0x519320))
#define shipDeathCount ((int *)(sim_image+0x5192e0))
#define padPressed (*(unsigned short *)(sim_image+0x65f7b8))
#define padButtons (*(unsigned short *)(sim_image+0x665108))
#define steerAnalog (*(unsigned short *)(sim_image+0x65f7ce))
#define turnDivisor (*(unsigned short *)(sim_image+0x664848))
#define SetShake ((void (*)(int))(sim_image+0x13940))
#define SetFire ((void (*)(ShipData *,void *,short))(sim_image+0x288c0))
#define SetDitch ((void (*)(ShipData *,void *,int))(sim_image+0x28970))
#define SetBrake ((void (*)(ShipData *))(sim_image+0x28820))
#define CameraUpdate (*(void (**)())(sim_image+0x66510c))
#define UpdateCameraRaceExternal ((void (*)())(sim_image+0x13f70))
#define ELECTROED 0x8000
#define DESTROYED 0x8000
#define VIEW_IN 4
#define RACING 64
#define JUNC_LEFT 0x2000
#define PAD_BUTTON_THRUST 0x4000
#define racingLine ((VECTOR *)(sim_image+0x65d034))
typedef struct {short distance;unsigned short pad;long x,y,z;} SimRaceLine;
#define raceLines ((SimRaceLine *)(sim_image+0x65d030))
#define PlayNote ((int (*)(short,short,short,short,short))(sim_image+0x78e0))
#define FireWeapon ((void (*)(ShipData *,void *,short,short))(sim_image+0x22660))
#define newRand ((int (*)(void))(sim_image+0xc820))
#define calcMyXPos ((void (*)(ShipData *,short,short *,short *))(sim_image+0x5ac10))
#define lookForWayPast ((void (*)(VECTOR *,ShipData *,short,short))(sim_image+0x5aa30))
#define ratan2 ((long (*)(long,long))(sim_image+0x4eee0))
#define DroneShip2TrackBase ((void (*)(ShipData *,Face *,int))(sim_image+0x16330))
#define UpdatePlayerShip ((void (*)())(sim_image+0x63680))
#define sqrt sim_sqrt
static int sim_abs(int a) {return a<0?-a:a;}
#define abs sim_abs
typedef void WeaponData;
static short sim_neighbour(ShipData *ships,short shipNo,int direction) {
    short position,attempts,candidate;
    for(position=0;position<activeShipCount;++position)if(shipOrder[position][0]==shipNo)break;
    for(attempts=0;attempts<15;++attempts) {
        if(direction<0) {if(position==0)position=activeShipCount;--position;}
        else {++position;if(position>=activeShipCount)position=0;}
        candidate=shipOrder[position][0];
        if(!(ships[candidate].combatAttr&DESTROYED) && candidate!=shipNo)return candidate;
    }
    return -1;
}
#define getShipInfrontLAP(ships,no) sim_neighbour(ships,no,-1)
#define getShipBehindLAP(ships,no) sim_neighbour(ships,no,1)
static void moveDownCentre(VECTOR *v,ShipData *ships,Face *face,int no) {(void)ships;(void)face;(void)no;v->vx=v->vy=v->vz=0;}
static void followRaceLine(VECTOR *v,ShipData *ships,Face *face,int no) {
    int section=ships[no].nearSecNo,index=face->vertex[0];
    v->vx=racingLine[section].vx-track->vertices[index].vx;
    v->vy=racingLine[section].vy-track->vertices[index].vy;
    v->vz=racingLine[section].vz-track->vertices[index].vz;
}
static void sim_drone_update(ShipData *ships, Object **shipShapes, short shipNo, WeaponData *weaponIndex)
{
    (void)shipShapes;
    TrackSection *section;
    short         decide;
    short         front2;
    Vector        path1;
    char          humanNo;
    short         lateral;
    short         ahead;
    short         front;
    VECTOR        facePoint;
    short         i;
    ShipData     *humanShip;
    TrackSection *nextSection;
    short         raceDist;
    short         steer;
    short         aheadDiff;
    ShipData     *ship;
    ShipData     *aheadShip;
    short         humanMoveDist;
    short         boostStep;
    short         blocked;
    short         along;
    VECTOR        targetVector;
    short         raceDiff;
    short         weapon;
    short         zone;
    Vector        path2;
    int           magnitude;
    float         flat;
    int           alpha;
    Face         *face;
    VECTOR        bestPath;
    {
        VECTOR offset;
        short speedBonus[4];
        VECTOR direction;
        speedBonus[0] = 80;
        speedBonus[1] = 140;
        speedBonus[2] = 200;
        speedBonus[3] = 210;

        blocked = 0;
        moveDownCentre(&offset, ships, (Face *)0, shipNo);
        humanNo = ownShip;
        ship = ships + shipNo;
        /* Launch acceleration while boostCount counts down, afterwards the speed creeps up to its limit. */
        if (ship->boostCount > 0) {
            if(sim_legacy)ship->boostCount--;
            ship->updateCount = 0;
            if ((ship->combatAttr & 0x10) || (shipNo == ownShip)) {
                if ((ship->remoteMax + ((classSpeedMargin[shipClassIndex] * 32) / 64)) >= ship->speed) {
                    if(sim_dt==1) ship->speed += (ship->remoteMagnitude + classAccel[shipClassIndex]); else SIM_ADD(ship,speed,((ship->remoteMagnitude + classAccel[shipClassIndex]))*sim_dt);
                }
            } else if (ship->combatAttr & 0x20) {
                if ((ship->remoteMax + ((classSpeedMargin[shipClassIndex] * 32) / 35)) >= ship->speed) {
                    if(sim_dt==1) ship->speed += (ship->remoteMagnitude + classAccel[shipClassIndex]); else SIM_ADD(ship,speed,((ship->remoteMagnitude + classAccel[shipClassIndex]))*sim_dt);
                }
            } else if (ship->combatAttr & 0x40) {
                if ((ship->remoteMax + ((classSpeedMargin[shipClassIndex] * 32) / 27)) >= ship->speed) {
                    if(sim_dt==1) ship->speed += (ship->remoteMagnitude + classAccel[shipClassIndex]); else SIM_ADD(ship,speed,((ship->remoteMagnitude + classAccel[shipClassIndex]))*sim_dt);
                }
            }
        } else if (ship->autopilotCount == 0) {
            if (ship->speed <= ship->remoteMax) {
                if(sim_dt==1) ship->speed += ship->remoteMagnitude; else SIM_ADD(ship,speed,(ship->remoteMagnitude)*sim_dt);
            }
        } else {
            if ((ship->remoteMax * 2) >= ship->speed) {
                if(sim_dt==1) ship->speed += (ship->remoteMagnitude * 3); else SIM_ADD(ship,speed,((ship->remoteMagnitude * 3))*sim_dt);
            }
        }
        if (!(ship->attr & FLYING)) {
            /* Steer along the track base face; overtake a ship in front if it is close and slower. */
            face = track->faces + ship->nearTrkSect->faceStart;
            if (!(face->flags & TRACK_BASE)) {
                do {
                    face++;
                } while (!(face->flags & TRACK_BASE));
            }
            raceDist = raceLines[ship->nearSecNo].distance;
            humanShip = ships + humanNo;
            raceDiff = raceDist - raceLines[humanShip->nearSecNo].distance;
            if (raceDiff < 0) {
                raceDiff += numTrackSections;
            }
            ahead = getShipInfrontLAP(ships, shipNo);
            /* 32 keeps the no-ship case out of the branch that reads aheadShip. */
            aheadDiff = 32;
            if (ahead != -1) {
                aheadShip = &ships[ahead];
                aheadDiff = raceLines[aheadShip->nearSecNo].distance - raceDist;
                if (aheadDiff < 0) {
                    aheadDiff += numTrackSections;
                }
            }
            if ((aheadDiff < 32) && (ship->moveDist > aheadShip->moveDist)) {
                lookForWayPast(&offset, ships, shipNo, ahead);
            } else {
                followRaceLine(&offset, ships, face, shipNo);
            }
            /* Zone: how far ahead (+) or behind (-) the human ship is, in sections; 100 means out of range. */
            if (ship->autopilotCount == 0) {
                if (numTrackSections / 2 < raceDiff) {
                    raceDiff -= numTrackSections;
                }
                zone = 100;
                if ((raceDiff < 84) && (raceDiff > -80)) {
                    if ((raceDiff <= 80) && (raceDiff > 40)) {
                        zone = 3;
                    }
                    if ((raceDiff <= 40) && (raceDiff > 20)) {
                        zone = 2;
                    }
                    if ((raceDiff <= 20) && (raceDiff > 12)) {
                        zone = 1;
                    }
                    if ((raceDiff <= 12) && (raceDiff >= -8)) {
                        zone = 0;
                    }
                    if ((raceDiff < -8) && (raceDiff >= -20)) {
                        zone = -1;
                    }
                    if ((raceDiff < -20) && (raceDiff >= -40)) {
                        zone = -2;
                    }
                    if ((raceDiff < -40) && (raceDiff >= -80)) {
                        zone = -3;
                    }
                    humanMoveDist = humanShip->moveDist;
                    steer = classSteer[shipClassIndex];
                    boostStep = steer / 8;
                    if (ship->boostCount == 0) {
                        if ((getShipBehindLAP(ships, shipNo) == humanNo) && (activeShipCount > 3)) {
                            front = getShipInfrontLAP(ships, shipNo);
                            front2 = getShipInfrontLAP(ships, front);
                            aheadDiff = raceLines[ships[front2].nearSecNo].distance - raceLines[humanShip->nearSecNo].distance;
                            if (aheadDiff < 0) {
                                aheadDiff += numTrackSections;
                            }
                            if ((aheadDiff > 0) && (aheadDiff < 40)) {
                                blocked = 1;
                            }
                        }
                    }
                    if ((blocked == 0) && (ship->moveDist < humanMoveDist)) {
                        if ((zone == 2) || (zone == -2)) {
                            if (classSpeedLimit[shipClassIndex] / 2 < humanShip->speed) {
                                if (ship->combatAttr & 0x20) {
                                    if (ship->speed <= (ship->remoteMax + (steer / 4))) {
                                        if(sim_dt==1) ship->speed += (ship->remoteMagnitude + (boostStep / 4)); else SIM_ADD(ship,speed,((ship->remoteMagnitude + (boostStep / 4)))*sim_dt);
                                    }
                                } else if (ship->combatAttr & 0x40) {
                                    if (ship->speed <= (ship->remoteMax + (steer / 4))) {
                                        if(sim_dt==1) ship->speed += (ship->remoteMagnitude + (boostStep / 4)); else SIM_ADD(ship,speed,((ship->remoteMagnitude + (boostStep / 4)))*sim_dt);
                                    }
                                }
                            }
                        }
                        if ((ship->moveDist < humanMoveDist) && ((zone == 1) || (zone == 0) || (zone == -1))) {
                            if (classSpeedLimit[shipClassIndex] / 2 < humanShip->speed) {
                                if (ship->combatAttr & 0x20) {
                                    if (ship->speed <= (ship->remoteMax + (steer / 2))) {
                                        if(sim_dt==1) ship->speed += (ship->remoteMagnitude + (boostStep / 2)); else SIM_ADD(ship,speed,((ship->remoteMagnitude + (boostStep / 2)))*sim_dt);
                                    }
                                } else if (ship->combatAttr & 0x40) {
                                    if (ship->speed <= (ship->remoteMax + (steer / 2))) {
                                        if(sim_dt==1) ship->speed += (ship->remoteMagnitude + (boostStep / 2)); else SIM_ADD(ship,speed,((ship->remoteMagnitude + (boostStep / 2)))*sim_dt);
                                    }
                                }
                            }
                        }
                    }
                }
                /* Fire the drone's weapon according to its temperament and the zone. */
                weapon = ship->weapon;
                if (sim_legacy && (weapon != 0) && (zone != 100) && !(humanShip->combatAttr & DESTROYED)) {
                    if (ship->combatAttr & 0x20) {
                        if ((zone == -3) && ((weapon == 1) || (weapon == 2) || (weapon == 5))) {
                            if (weapon == 1) PlayNote(22, 510, 15, 15, 256);
                            if (weapon == 2) PlayNote(21, 510, 15, 15, 256);
                            if (weapon == 5) PlayNote(24, 510, 15, 15, 256);
                            ship->targetShip = -1;
                            FireWeapon(ship, weaponIndex, shipNo, ship->weapon);
                        } else if (zone == 1) {
                            if (weapon == 3) {
                                FireWeapon(ship, weaponIndex, shipNo, weapon);
                                PlayNote(20, 470, 15, 15, 256);
                            } else if (weapon == 2) {
                                ship->targetShip = humanNo;
                                FireWeapon(ship, weaponIndex, shipNo, weapon);
                                PlayNote(21, 510, 15, 15, 256);
                            }
                        }
                    } else if (ship->combatAttr & 0x40) {
                        if (((zone == -3) || (zone == -2)) && ((weapon == 1) || (weapon == 2) || (weapon == 5))) {
                            ship->targetShip = humanNo;
                            FireWeapon(ship, weaponIndex, shipNo, weapon);
                            if (weapon == 1) PlayNote(22, 510, 15, 15, 256);
                            if (weapon == 2) PlayNote(21, 510, 15, 15, 256);
                            if (weapon == 5) PlayNote(24, 510, 15, 15, 256);
                        }
                        if (zone == 1) {
                            if (weapon == 3) {
                                FireWeapon(ship, weaponIndex, shipNo, ship->weapon);
                                PlayNote(20, 470, 15, 15, 256);
                            } else if (weapon == 2) {
                                ship->targetShip = humanNo;
                                FireWeapon(ship, weaponIndex, shipNo, ship->weapon);
                                PlayNote(21, 510, 15, 15, 256);
                            }
                        }
                    }
                    if ((weapon == 8) && (zone >= 1) && (zone <= 3)) {
                        FireWeapon(ship, weaponIndex, shipNo, ship->weapon);
                    }
                }
            }
            /* Pick the branch to take at a junction: pit lane when due, otherwise at random. */
            section = ship->nearTrkSect->prevSection.ptr;
            for (i = 0; i < 4; i++) {
                section = section->nextSection.ptr;
            }
            if ((section->junction.ptr != (TrackSection *)-1) && (section->junction.ptr->flags & 0x10)) {
                if ((section->secNo == pitLaneSection1[clipLevel]) || (section->secNo == pitLaneSection2[clipLevel])) {
                    int damage = shipDamage[shipNo];

                    ship->attr &= ~JUNC_LEFT;
                    if (damage > 0) {
                        ship->attr |= pitLaneDirection[clipLevel];
                    } else {
                        ship->attr |= pitLaneDirection[clipLevel] ^ JUNC_LEFT;
                    }
                } else {
                    decide = sim_legacy?newRand() % 2:((ship->attr & JUNC_LEFT)?0:1);
                    if (decide == 0) {
                        ship->attr |= JUNC_LEFT;
                    } else {
                        ship->attr &= ~JUNC_LEFT;
                    }
                }
            }
            /* Keep the current section as the base while the lookahead follows junctions. */
            section = ship->nearTrkSect;
            nextSection = section->prevSection.ptr;
            for (i = 0; i < 4; i++) {
                if ((nextSection->junction.ptr != (TrackSection *)-1) && (nextSection->junction.ptr->flags & 0x10)
                    && (ship->attr & JUNC_LEFT)) {
                    nextSection = nextSection->junction.ptr;
                } else {
                    nextSection = nextSection->nextSection.ptr;
                }
            }
            /* Target velocity along the track, bled off by turning, then the pull towards the racing line. */
            nextSection = nextSection->nextSection.ptr;
            if(sim_dt==1) ship->speed -= abs(ship->speed * ship->vhdg) >> 14; else SIM_DECAY(ship,speed,1-sim_abs(ship->vhdg)/16384.0);
            if(sim_dt==1) ship->speed -= abs(ship->speed * ship->vpitch) >> 14; else SIM_DECAY(ship,speed,1-sim_abs(ship->vpitch)/16384.0);
            if ((section->flags & 1) || (section->nextSection.ptr->flags & 1)
                || (section->prevSection.ptr->flags & 1)
                || (section->nextSection.ptr->nextSection.ptr->flags & 1)
                || (section->nextSection.ptr->nextSection.ptr->nextSection.ptr->flags & 1)) {
                targetVector.vx = section->centre.vx - section->prevSection.ptr->centre.vx;
                targetVector.vy = section->centre.vy - section->prevSection.ptr->centre.vy;
                targetVector.vz = section->centre.vz - section->prevSection.ptr->centre.vz;
            } else {
                targetVector.vx = nextSection->centre.vx - section->centre.vx;
                targetVector.vy = nextSection->centre.vy - section->centre.vy;
                targetVector.vz = nextSection->centre.vz - section->centre.vz;
            }
            magnitude = GetMagnitude(targetVector.vx, targetVector.vy, targetVector.vz);
            targetVector.vx = (targetVector.vx * ship->speed) / magnitude;
            targetVector.vy = (targetVector.vy * ship->speed) / magnitude;
            targetVector.vz = (targetVector.vz * ship->speed) / magnitude;
            path1.vx = section->centre.vx + offset.vx;
            path1.vy = section->centre.vy + offset.vy;
            path1.vz = section->centre.vz + offset.vz;
            path2.vx = nextSection->centre.vx + offset.vx;
            path2.vy = nextSection->centre.vy + offset.vy;
            path2.vz = nextSection->centre.vz + offset.vz;
            GetTargetPos(&bestPath, ship->ppivot, path2, path1);
            ship->apivot.vx = targetVector.vx + (bestPath.vx - ship->ppivot.vx);
            ship->apivot.vy = targetVector.vy + (bestPath.vy - ship->ppivot.vy);
            ship->apivot.vz = targetVector.vz + (bestPath.vz - ship->ppivot.vz);
            if(sim_dt==1) ship->vpivot.vx += ship->apivot.vx; else SIM_ADD(ship,vpivot.vx,(ship->apivot.vx)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            if(sim_dt==1) ship->vpivot.vy += ship->apivot.vy; else SIM_ADD(ship,vpivot.vy,(ship->apivot.vy)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            if(sim_dt==1) ship->vpivot.vz += ship->apivot.vz; else SIM_ADD(ship,vpivot.vz,(ship->apivot.vz)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            /* Speed-up pads boost the drone when it drives over them on the matching side. */
            calcMyXPos(ships, shipNo, &lateral, &along);
            if ((face->flags & SPEED_UP) && (lateral < 0)) {
                if(sim_dt==1) ship->speed += speedBonus[(unsigned short)shipClassIndex]; else SIM_ADD(ship,speed,(speedBonus[(unsigned short)shipClassIndex])*sim_dt);
            }
            face++;
            if ((face->flags & SPEED_UP) && (lateral > 0)) {
                if(sim_dt==1) ship->speed += speedBonus[(unsigned short)shipClassIndex]; else SIM_ADD(ship,speed,(speedBonus[(unsigned short)shipClassIndex])*sim_dt);
            }
            facePoint.vx = track->vertices[face->vertex[0]].vx;
            facePoint.vy = track->vertices[face->vertex[0]].vy;
            facePoint.vz = track->vertices[face->vertex[0]].vz;
            alpha = PlaneLineCollision(facePoint, ship->ppivot, face->normal);
            if (alpha < 50) {
                alpha = 50;
            }
            ship->apivot.vx = ((face->normal.vx * 256 * 64) / alpha - (face->normal.vx * 64)) >> 8;
            ship->apivot.vy = ((face->normal.vy * 256 * 64) / alpha - (face->normal.vy * 64)) >> 8;
            ship->apivot.vz = ((face->normal.vz * 256 * 64) / alpha - (face->normal.vz * 64)) >> 8;
            if(sim_dt==1) ship->vpivot.vx += ship->apivot.vx; else SIM_ADD(ship,vpivot.vx,(ship->apivot.vx)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            if(sim_dt==1) ship->vpivot.vy += ship->apivot.vy; else SIM_ADD(ship,vpivot.vy,(ship->apivot.vy)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            if(sim_dt==1) ship->vpivot.vz += ship->apivot.vz; else SIM_ADD(ship,vpivot.vz,(ship->apivot.vz)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            /* Heading, pitch and roll follow the target direction. */
            flat = sqrt(targetVector.vx * targetVector.vx + targetVector.vz * targetVector.vz);
            ship->vhdg = -ratan2(targetVector.vx, targetVector.vz) - ship->hdg;
            ship->vhdg += ship->ahdg;
            if(sim_dt==1) ship->ahdg -= ship->ahdg >> 3; else SIM_DECAY(ship,ahdg,.875);
            if (ship->vhdg > 2048) {
                ship->vhdg = ship->vhdg - 4096;
            } else if (ship->vhdg < -2048) {
                ship->vhdg = ship->vhdg + 4096;
            }
            if(sim_dt==1) ship->hdg += ship->vhdg >> 4; else SIM_ADD(ship,hdg,((ship->vhdg / 16.0))*(16*(1-sim_sqrt(15.0/16))));
            ship->vpitch = -ratan2(targetVector.vy, flat) - ship->pitch;
            if(sim_dt==1) ship->pitch += ship->vpitch >> 4; else SIM_ADD(ship,pitch,((ship->vpitch / 16.0))*(16*(1-sim_sqrt(15.0/16))));
            if(sim_dt==1) ship->vroll += ship->vhdg / 4; else SIM_ADD(ship,vroll,(ship->vhdg / 4.0)*(sim_sqrt(.5)/(1+sim_sqrt(.5))));
            if(sim_dt==1) ship->vroll -= ship->vroll >> 1; else SIM_DECAY(ship,vroll,.5);
            if(sim_dt==1) ship->roll += ship->vroll; else SIM_ADD(ship,roll,(ship->vroll)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            if(sim_dt==1) ship->roll -= ship->roll >> 3; else SIM_DECAY(ship,roll,.875);
            facePoint.vx = track->vertices[face->vertex[0]].vx;
            facePoint.vy = track->vertices[face->vertex[0]].vy;
            facePoint.vz = track->vertices[face->vertex[0]].vz;
            if (ship->autopilotCount > 0) {
                alpha = PlaneLineCollision(facePoint, ship->ppivot, face->normal);
                if (alpha < -300) {
                    alpha = -300;
                }
                DroneShip2TrackBase(ship, face, alpha);
                section = ship->nearTrkSect;
                nextSection = section->nextSection.ptr;
                direction.vx = nextSection->centre.vx - section->centre.vx;
                direction.vy = nextSection->centre.vy - ship->nearTrkSect->centre.vy;
                direction.vz = nextSection->centre.vz - ship->nearTrkSect->centre.vz;
                if (((direction.vx * ship->unitVecNose.vx) + (direction.vy * ship->unitVecNose.vy) + (direction.vz * ship->unitVecNose.vz)) < 0) {
                    ship->attr &= ~DIRECTION;
                } else {
                    ship->attr |= DIRECTION;
                }
            }
        } else {
            section = ship->nearTrkSect->nextSection.ptr->nextSection.ptr;
            nextSection = section->nextSection.ptr;
            moveDownCentre(&offset, ships, (Face *)0, shipNo);
            if(sim_dt==1) ship->speed -= abs(ship->speed * ship->vhdg) >> 14; else SIM_DECAY(ship,speed,1-sim_abs(ship->vhdg)/16384.0);
            targetVector.vx = nextSection->centre.vx - section->centre.vx;
            targetVector.vy = nextSection->centre.vy - section->centre.vy;
            targetVector.vz = nextSection->centre.vz - section->centre.vz;
            magnitude = GetMagnitude(targetVector.vx, targetVector.vy, targetVector.vz);
            targetVector.vx = (targetVector.vx * ship->speed) / magnitude;
            targetVector.vz = (targetVector.vz * ship->speed) / magnitude;
            targetVector.vy = 500;
            GetTargetPos(&bestPath, ship->ppivot, nextSection->centre, ship->nearTrkSect->centre);
            ship->apivot.vx = targetVector.vx + ((bestPath.vx - ship->ppivot.vx) >> 1);
            ship->apivot.vy = targetVector.vy;
            ship->apivot.vz = targetVector.vz + ((bestPath.vz - ship->ppivot.vz) >> 1);
            if(sim_dt==1) ship->vpivot.vx += ship->apivot.vx; else SIM_ADD(ship,vpivot.vx,(ship->apivot.vx)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            if(sim_dt==1) ship->vpivot.vy += ship->apivot.vy; else SIM_ADD(ship,vpivot.vy,(ship->apivot.vy)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            if(sim_dt==1) ship->vpivot.vz += ship->apivot.vz; else SIM_ADD(ship,vpivot.vz,(ship->apivot.vz)*(sim_sqrt(.875)/(1+sim_sqrt(.875))));
            ship->vpitch = -200 - ship->pitch;
            if(sim_dt==1) ship->pitch += ship->vpitch >> 4; else SIM_ADD(ship,pitch,((ship->vpitch / 16.0))*(16*(1-sim_sqrt(15.0/16))));
            ship->vhdg = -ratan2(targetVector.vx, targetVector.vz) - ship->hdg;
            if (ship->vhdg > 2048) {
                ship->vhdg = ship->vhdg - 4096;
            } else if (ship->vhdg < -2048) {
                ship->vhdg = ship->vhdg + 4096;
            }
            if(sim_dt==1) ship->hdg += ship->vhdg >> 4; else SIM_ADD(ship,hdg,((ship->vhdg / 16.0))*(16*(1-sim_sqrt(15.0/16))));
        }
    }
    if(sim_dt==1) ship->vpivot.vx -= ship->vpivot.vx >> 3; else SIM_DECAY(ship,vpivot.vx,.875);
    if(sim_dt==1) ship->vpivot.vy -= ship->vpivot.vy >> 3; else SIM_DECAY(ship,vpivot.vy,.875);
    if(sim_dt==1) ship->vpivot.vz -= ship->vpivot.vz >> 3; else SIM_DECAY(ship,vpivot.vz,.875);
    if(sim_dt==1) ship->ppivot.vx += ship->vpivot.vx >> 6; else SIM_ADD(ship,ppivot.vx,((ship->vpivot.vx / 64.0))*sim_dt);
    if(sim_dt==1) ship->ppivot.vy += ship->vpivot.vy >> 6; else SIM_ADD(ship,ppivot.vy,((ship->vpivot.vy / 64.0))*sim_dt);
    if(sim_dt==1) ship->ppivot.vz += ship->vpivot.vz >> 6; else SIM_ADD(ship,ppivot.vz,((ship->vpivot.vz / 64.0))*sim_dt);
    if (sim_legacy && (ship->attr & ELECTROED)) {
        if(sim_dt==1) ship->ppivot.vx += (newRand() - 0x4000) % 20; else SIM_ADD(ship,ppivot.vx,((newRand() - 0x4000) % 20)*sim_dt);
        if(sim_dt==1) ship->ppivot.vy += (newRand() - 0x4000) % 20; else SIM_ADD(ship,ppivot.vy,((newRand() - 0x4000) % 20)*sim_dt);
        if(sim_dt==1) ship->ppivot.vz += (newRand() - 0x4000) % 20; else SIM_ADD(ship,ppivot.vz,((newRand() - 0x4000) % 20)*sim_dt);
        if(sim_legacy)ship->electroCount--;
        if (ship->electroCount <= 0) {
            ship->electroCount = 0;
            ship->attr &= ~ELECTROED;
        }
        if ((newRand() % 10) == 0) {
            if(sim_dt==1) ship->speed -= ship->speed >> 1; else SIM_ADD(ship,speed,-(ship->speed >> 1)*sim_dt);
        }
    }
    ship->hdg = ang(ship->hdg);
    ship->pitch = ang(ship->pitch);
    ship->roll = ang(ship->roll);
    if (ship->autopilotCount > 0) {
        if (padPressed & 0x8000) {
            ship->update = UpdatePlayerShip;
            ship->vhdg = 0;
            ship->autopilotCount = 0;
            if (shipNo == ownShip) {
                PlayNote(46, 255, 15, 15, 256);
            }
        } else {
            if(sim_legacy)ship->autopilotCount--;
            if (ship->autopilotCount == 0) {
                if ((ship->roll > -50) && (ship->roll < 50)) {
                    ship->update = UpdatePlayerShip;
                    ship->vhdg = 0;
                    return;
                }
                ship->autopilotCount = 1;
            }
        }
    }
}
