#define ShipTrkReaction sim_track_reaction
#define SAMPLE_NEW_TURBO 34
#define SAMPLE_SHIP_STALL 36
static void sim_player_update(ShipData *shipIndex, void *shapes, short shipNo, void *weapons)
{
    short     upper;
    short     destroyed;
    short     thrust;
    short     lower;
    ShipData *playerShip;
    short     launchThrust;
    short     maxThrust;
    short     targetTurn;

    destroyed = 0;
    playerShip = &shipIndex[shipNo];
    if (shipDeathCount[shipNo] != -1) {
        ShipTrkReaction(playerShip, shapes, shipNo);
        return;
    }
    if (playerShip->combatAttr & DESTROYED) {
        return;
    }
    if (playerShip->electroCount > 0) {
        if(sim_legacy)playerShip->electroCount--;
    } else if (playerShip->electroCount == 0) {
        playerShip->attr &= ~ELECTROED;
    }
    targetTurn = (((steerAnalog - 128) * (-400 - playerShip->maxHeading)) << 1) / turnDivisor;
    if (playerShip->vhdg < targetTurn) {
        if (targetTurn - playerShip->vhdg <= playerShip->headingInc) {
            if(sim_dt==1) playerShip->vhdg += targetTurn - playerShip->vhdg; else SIM_ADD(playerShip,vhdg,(targetTurn - playerShip->vhdg)*sim_dt);
        } else if (playerShip->vhdg >= 0) {
            if(sim_dt==1) playerShip->vhdg += playerShip->headingInc; else SIM_ADD(playerShip,vhdg,(playerShip->headingInc)*sim_dt);
        } else if (playerShip->vhdg < 0) {
            if(sim_dt==1) playerShip->vhdg += playerShip->headingInc << 1; else SIM_ADD(playerShip,vhdg,(playerShip->headingInc << 1)*sim_dt);
        }
    } else if (playerShip->vhdg > targetTurn) {
        if (targetTurn - playerShip->vhdg >= -playerShip->headingInc) {
            if(sim_dt==1) playerShip->vhdg += targetTurn - playerShip->vhdg; else SIM_ADD(playerShip,vhdg,(targetTurn - playerShip->vhdg)*sim_dt);
        }
        if (playerShip->vhdg <= 0) {
            if(sim_dt==1) playerShip->vhdg -= playerShip->headingInc; else SIM_ADD(playerShip,vhdg,-(playerShip->headingInc)*sim_dt);
        } else {
            if(sim_dt==1) playerShip->vhdg -= playerShip->headingInc << 1; else SIM_ADD(playerShip,vhdg,-(playerShip->headingInc << 1)*sim_dt);
        }
    }
    if (playerShip->vhdg < -playerShip->maxHeading) {
        playerShip->vhdg = -playerShip->maxHeading;
    }
    if (playerShip->vhdg > playerShip->maxHeading) {
        playerShip->vhdg = playerShip->maxHeading;
    }
    if (sim_legacy && (playerShip->attr & ELECTROED)) {
        if (shipNo == ownShip && (playerShip->attr & VIEW_IN)) {
            SetShake(2);
        }
        if(sim_dt==1) playerShip->vhdg += newRand() % 600 - 300; else SIM_ADD(playerShip,vhdg,(newRand() % 600 - 300)*sim_dt);
    }
    if (padButtons & 0x40) {
        if(sim_dt==1) playerShip->vpitch += 30; else SIM_ADD(playerShip,vpitch,(30)*sim_dt);
    } else if (padButtons & 0x10) {
        if(sim_dt==1) playerShip->vpitch -= 30; else SIM_ADD(playerShip,vpitch,-(30)*sim_dt);
    }
    if (sim_legacy && playerShip->updateCount == 90) {
        maxThrust = playerShip->maxThrust;
        launchThrust = maxThrust >> 3;
        upper = launchThrust * 6;
        thrust = playerShip->thrust;
        lower = launchThrust * 5;
        if (lower <= thrust) {
            if (thrust < upper) {
                playerShip->thrustCount = 9;
                playerShip->thrust = maxThrust;
                if (shipNo == ownShip) {
                    PlayNote(SAMPLE_NEW_TURBO, 0xFF, 0xF, 0xF, 0x100);
                }
            } else {
                playerShip->stallCount = 15;
                if (shipNo == ownShip) {
                    PlayNote(SAMPLE_SHIP_STALL, 0xFF, 0xF, 0xF, 0x100);
                }
            }
        }
    }
    if (playerShip->thrustCount > 0) {
        if(sim_legacy)playerShip->thrustCount--;
    }
    if (playerShip->updateCount > 0) {
        if(sim_legacy)playerShip->updateCount--;
    }
    if (!(playerShip->combatAttr & 8)) {
        maxThrust = playerShip->maxThrust;
        if (padButtons & PAD_BUTTON_THRUST) {
            if(sim_dt==1) playerShip->thrust += 16; else SIM_ADD(playerShip,thrust,(16)*sim_dt);
        } else if (playerShip->thrust >= 8) {
            if(sim_dt==1) playerShip->thrust -= 8; else SIM_ADD(playerShip,thrust,-(8)*sim_dt);
        }
        if (playerShip->thrust > maxThrust) {
            playerShip->thrust = maxThrust;
        }
    } else {
        if(sim_dt==1) playerShip->thrust -= 64; else SIM_ADD(playerShip,thrust,-(64)*sim_dt);
        if (playerShip->thrust < 0) {
            playerShip->thrust = 0;
            if (playerShip->speed < 5000U) {
                destroyed = 1;
            }
        }
    }
    if (sim_legacy && (playerShip->attr & ELECTROED) && newRand() % 80 == 0) {
        if(sim_dt==1) playerShip->thrust -= playerShip->thrust >> 2; else SIM_DECAY(playerShip,thrust,.75);
    }
    if (playerShip->stallCount > 0) {
        if(sim_legacy)playerShip->stallCount--;
        if(sim_dt==1) playerShip->thrust -= playerShip->thrust >> 2; else SIM_DECAY(playerShip,thrust,.75);
    }
    if(sim_dt==1)SetBrake(playerShip);
    else {
        if(padButtons&0x400)SIM_ADD(playerShip,r_brake_rot,32*sim_dt);
        else if(playerShip->r_brake_rot>0)SIM_ADD(playerShip,r_brake_rot,-32*sim_dt);
        if(padButtons&0x800)SIM_ADD(playerShip,l_brake_rot,32*sim_dt);
        else if(playerShip->l_brake_rot>0)SIM_ADD(playerShip,l_brake_rot,-32*sim_dt);
        if(playerShip->r_brake_rot<0)playerShip->r_brake_rot=0;
        if(playerShip->r_brake_rot>256)playerShip->r_brake_rot=256;
        if(playerShip->l_brake_rot<0)playerShip->l_brake_rot=0;
        if(playerShip->l_brake_rot>256)playerShip->l_brake_rot=256;
    }
    if(sim_legacy)SetFire(playerShip, weapons, shipNo);
    if(sim_legacy)((void (*)(ShipData *, void *, int))SetDitch)(playerShip, weapons, shipNo);
    ShipTrkReaction(playerShip, shapes, shipNo);
    if (destroyed) {
        shipDeathCount[shipNo] = 100;
        playerShip->combatAttr |= 4;
        if (playerShip->attr & VIEW_IN) {
            playerShip->attr &= ~VIEW_IN;
            CameraUpdate = UpdateCameraRaceExternal;
        }
    }
}
