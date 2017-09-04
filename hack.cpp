#include "hack.h"
Config cfg;
unsigned int offsets::m_pStudioBones =  0x2C70; //unsigned long long m_pStudioBones;
unsigned int offsets::m_flLowerBodyYawTarget = 0x42C8; //float m_flLowerBodyYawTarget; // 0x42C8
unsigned int offsets::dwViewMatrix = 0x2537374; //4x4 float matrix //2537334 and 2553C14 on Aug-7-17
unsigned int offsets::m_lifeState = 0x293; //int m_lifeState; // 0x293
unsigned int offsets::m_bIsDefusing = 0x416C;//unsigned char m_bIsDefusing; // 0x416C
unsigned int offsets::m_bIsScoped = 0x4164;//unsigned char m_bIsScoped; // 0x4164
unsigned int offsets::m_bGunGameImmunity = 0x4178;   	//unsigned char m_bGunGameImmunity; // 0x4178
unsigned int offsets::m_iShotsFired = 0xABB0;    	//int m_iShotsFired; // 0xABB0
unsigned int offsets::m_aimPunchAngle = 0x3764;    	//Vector m_aimPunchAngle; // 0x3764
unsigned int offsets::m_fFlashMaxAlpha = 0xabd4; //flash max alpha 0-255 float value

void hack::readEntities(std::array<EntityInfo,64> &rentities){
    // get shared access
    boost::shared_lock<boost::shared_mutex> lock(entities_access);
    //cout<<"read ent list"<<endl;
    rentities=entities;
}

void hack::writeEntities(std::array<EntityInfo,64> &wentities){
    // get upgradable access
    boost::upgrade_lock<boost::shared_mutex> lock(entities_access);

    // get exclusive access
    boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

    entities=wentities;
    //cout<<"wrote ent list"<<endl;
}
bool hack::getWorldToScreenData(std::array<EntityToScreen,64> &output){
    if(!hack::isConnected){
        return false;
    }

    std::array<EntityInfo,64> entitiesForScreen;
    hack::readEntities(entitiesForScreen);
    float myViewMatrix[16];
    unsigned long localPlayer;
    Entity myEnt;

    if(!csgo.Read((void*)client.client_start+offsets::dwViewMatrix,&myViewMatrix,sizeof(float)*16)){
       return false;
    }
    if(!csgo.Read((void*)m_addressOfLocalPlayer,&localPlayer,sizeof(long))){
        return false;
    }
    if(localPlayer==0){
        return false;
    }
    csgo.Read((void*)localPlayer,&myEnt,sizeof(Entity));
    for (int i = 0;i<64;i++){
        if(entitiesForScreen[i].entity.m_iHealth<=0||entitiesForScreen[i].entity.m_bDormant||entitiesForScreen[i].entity.ID==myEnt.ID||entitiesForScreen[i].entityPtr==0){
            continue;
        }
        Vector vecScreenOrigin;
        Vector vecScreenHeight;
        Vector height = entitiesForScreen[i].entity.m_vecAbsOrigin;
        height.z+=68;
        if(helper::WorldToScreen(entitiesForScreen[i].entity.m_vecAbsOrigin,vecScreenOrigin,myViewMatrix)&&helper::WorldToScreen(height,vecScreenHeight,myViewMatrix)){
            output[i].origin=vecScreenOrigin;
            output[i].head=vecScreenHeight;
        }
        int isScoped = false;
        int isDefusing = false;
        float lby = 0;
        csgo.Read((void*)entitiesForScreen[i].entityPtr+offsets::m_bIsScoped,&isScoped,sizeof(char));
        csgo.Read((void*)entitiesForScreen[i].entityPtr+offsets::m_bIsDefusing,&isDefusing,sizeof(char));
        csgo.Read((void*)entitiesForScreen[i].entityPtr+offsets::m_flLowerBodyYawTarget,&lby,sizeof(float));
        output[i].myTeam=myEnt.m_iTeamNum==entitiesForScreen[i].entity.m_iTeamNum;
        output[i].scoped=isScoped>0?true:false;
        output[i].defusing=isDefusing;
        output[i].lby = lby;
        output[i].entityInfo = entitiesForScreen[i];
    }
    return true;
}
void hack::setIsConnected(){
    int isConnected;
    csgo.Read((void*)hack::addressIsConnected,&isConnected,sizeof(char));
    if(isConnected!=1){
        hack::isConnected=false;
    }
    hack::isConnected=true;
}

void hack::aim(){
    QAngle newAngle;

    Vector myPos;
    Entity myEnt;
    QAngle punch;
    static QAngle oldPunch;
    QAngle punchDelta;
    QAngle viewAngle;
    QAngle aimDelta;
    unsigned int shotsFired = 0;
    int AltTwo = 4;

    Vector theirPos;
    BoneMatrix theirBones;

    //mem addresses
    unsigned int serverDetail = 0;
    unsigned long one = 0;
    unsigned long two = 0;
    unsigned long localPlayer = 0;
    unsigned long addressOfViewAngle = 0;

    static bool foundTarget;//true while found a living target (not necessarily in fov)
    bool shouldShoot= false;//true if our crosshair is close enough to the enemy and we should shoot
    static bool isAiming;//true if mouse was clicked when foundTarget == true. remains true until mouse is released. used to determine if we should snap to next target (based on rage mode or legit mode setting)
    static bool acquiring;//true if mouse clicked but didnt reach target yet. used to finish a shot in progress.

    static unsigned int idclosestEnt = 0;
    float lowestDistance = -1.0;//used to determine the closest target in the entity loop

    std::array<EntityInfo,64> entitiesCopy;

    hack::readEntities(entitiesCopy);

    csgo.Read((void*) m_addressOfLocalPlayer, &localPlayer, sizeof(long));
    if(localPlayer==0){
        return;
    }
    csgo.Read((void*)m_addressOfAlt2,&AltTwo,sizeof(long));
    csgo.Read((void*)hack::addressServerDetail,&serverDetail, sizeof(int));
    csgo.Read((void*)hack::basePointerOfViewAngle,&one,sizeof(long));
    csgo.Read((void*)one+serverDetail,&two,sizeof(long));
    csgo.Read((void*)two+0x4308,&addressOfViewAngle,sizeof(long));
    addressOfViewAngle+=0x8e20;
    csgo.Read((void*)(unsigned long)localPlayer,&myEnt,sizeof(Entity));
    csgo.Read((void*)(localPlayer+offsets::m_aimPunchAngle),&punch,sizeof(QAngle));
    csgo.Read((void*)(localPlayer+offsets::m_iShotsFired), &shotsFired,sizeof(int));
    csgo.Read((void*)(unsigned long)addressOfViewAngle, &(viewAngle),sizeof(QAngle));


    char enemyLifeState=LIFE_DEAD;
    enemyLifeState = hack::getLifeState((unsigned long)entitiesCopy[idclosestEnt].entityPtr);
    if(enemyLifeState!=LIFE_ALIVE||idclosestEnt==0){//if the enemy we were aiming at is not alive or is the world...
        acquiring=false;//stop acquiring the target
        foundTarget=false;//no target found
    }

    punchDelta.x = punch.x-oldPunch.x;
    punchDelta.y = punch.y-oldPunch.y;

    newAngle = viewAngle;

    myPos.x = myEnt.m_vecNetworkOrigin.x;
    myPos.y = myEnt.m_vecNetworkOrigin.y;
    myPos.z = myEnt.m_vecNetworkOrigin.z + myEnt.m_vecViewOffset.z;

    if(!isAiming||rage){//if we aren't aiming or are in rage mode...
        idclosestEnt=0;
        //aimbot block
        for(int i = 1;i<64;i++){
            void* entPtr = entitiesCopy[i].entityPtr;
            int lifeState = hack::getLifeState((unsigned long)entPtr);;
            if((shootFriends||((entitiesCopy[i].entity.m_iTeamNum == 3 && myEnt.m_iTeamNum==2)||(entitiesCopy[i].entity.m_iTeamNum ==2 && myEnt.m_iTeamNum==3)))
                    &&(entitiesCopy[i].entity.m_bDormant==0)
                    &&(entitiesCopy[i].entity.m_iHealth>0)
                    &&(entitiesCopy[i].entity.ID!=myEnt.ID)
                    &&(lifeState==LIFE_ALIVE)){//if the ent isn't on our team, if they are not dormant, if their health is greater than 0, if they are not us, if they are alive...
                void* m_pStudioBones;
                int closestBone;
                /*float lby;
                csgo.Read((void*)entPtr+offsets::m_flLowerBodyYawTarget,&lby,sizeof(float));
                //auto b-aim
                helper::clampAngle(&entitiesCopy[i].entity.m_angNetworkAngles);
                float angleDiff = fabsf(lby-entitiesCopy[i].entity.m_angNetworkAngles.y);
                //cout<<"angleDiff "<<angleDiff<<endl;
                if(angleDiff>35){
                    closestBone=6;
                }
                else{
                    closestBone = hack::getClosestBone((unsigned long)m_pStudioBones, hack::bones, viewAngle, punch, myPos);
                }*/
                csgo.Read((void*)entPtr+offsets::m_pStudioBones,&m_pStudioBones,sizeof(int));
                closestBone = hack::getClosestBone((unsigned long)m_pStudioBones, hack::bones, viewAngle, punch, myPos);
                csgo.Read(m_pStudioBones+closestBone*sizeof(BoneMatrix),&theirBones,sizeof(BoneMatrix));
                theirPos.x = theirBones.x;
                theirPos.y = theirBones.y;
                theirPos.z = theirBones.z;
                //cout<<"myPos x, y, z "<<myPos.x<<", "<<myPos.y<<", "<<myPos.z<<endl;
                if(theirPos.y==0&&theirPos.x==0&&theirPos.z==0){//if we find invalid data...
                    continue;//skip this iteration
                }
                //cout<<"theirPos x, y, z "<<theirPos.x<<", "<<theirPos.y<<", "<<theirPos.z<<endl;

                /* failed at creating a resolver
                float lby;
                csgo.Read((void*)entPtr+offsets::m_flLowerBodyYawTarget,&lby,sizeof(int));
                if(true){
                      hack::resolve(&entitiesCopy[i].entity,&theirPos,lby);
                }*/

                aimDelta=helper::calcAngle(&myPos,&theirPos);
                aimDelta.x -=viewAngle.x+(punch.x*2);//calculate aimDelta in order to calculate xhair distance. aimDelta is angle to the player - (current angle + punch*2)
                aimDelta.y -=viewAngle.y+(punch.y*2);
                helper::clampAngle(&aimDelta);
                //cout<<"vecabs x "<<entitiesCopy[i].entity.m_vecAbsOrigin.x<<" y "<<entitiesCopy[i].entity.m_vecAbsOrigin.y<<" z "<<entitiesCopy[i].entity.m_vecAbsOrigin.z<<endl;
                //cout<<"vec x "<<entitiesCopy[i].entity.m_vecOrigin.x<<" y "<<entitiesCopy[i].entity.m_vecOrigin.y<<" z "<<entitiesCopy[i].entity.m_vecOrigin.z<<endl;
                //cout<<"networked angles x "<<entitiesCopy[i].entity.m_angNetworkAngles.x<<" y "<<entitiesCopy[i].entity.m_angNetworkAngles.y<<" z "<<entitiesCopy[i].entity.m_angNetworkAngles.z<<endl;
                //cout<<"lby: "<<lby<<endl;
                //cout<< "aimDelta.x, y = "<<aimDelta.x<<", "<<aimDelta.y<<endl;
                float xhairDistance = helper::getDistanceFov(&aimDelta,&myPos,&theirPos);
                //cout<< "aimDelta.x, y = "<<aimDelta.x<<", "<<aimDelta.y<<" xhair, entid: "<<xhairDistance<<" "<<i<<endl;
                //cout<<lowestDistance<<" <- lowest - cur distance -> "<<xhairDistance<<endl;
                //cout<<" i "<<i<<" entID "<<entitiesCopy[i].entity.ID<<endl;
                if(xhairDistance<=lowestDistance||lowestDistance==-1.0){
                    lowestDistance=xhairDistance;
                    idclosestEnt = entitiesCopy[i].entity.ID;
                    foundTarget = true;
                    //cout<<foundTarget<<endl;
                }
            }
        }
    }
    //cout<<"idclosestEnt "<<idclosestEnt<<endl;
    if(foundTarget&&idclosestEnt!=0){//if we have an alive target...
        unsigned long m_pStudioBones;
        int closestBone;
        /*float lby;

        csgo.Read(entitiesCopy[idclosestEnt].entityPtr+offsets::m_flLowerBodyYawTarget,&lby,sizeof(float));

        //auto b-aim
        helper::clampAngle(&entitiesCopy[idclosestEnt].entity.m_angNetworkAngles);
        float angleDiff = fabsf(lby-entitiesCopy[idclosestEnt].entity.m_angNetworkAngles.y);
        cout<<"angleDiff "<<angleDiff<<endl;
        cout<<"lby "<<lby<<" entitiesCopy[idclosestEnt].entity.m_angNetworkAngles.y "<<entitiesCopy[idclosestEnt].entity.m_angNetworkAngles.y<<endl;
        if(angleDiff>35){
            closestBone=6;
        }
        else{
            closestBone = hack::getClosestBone((unsigned long)m_pStudioBones, hack::bones, viewAngle, punch, myPos);
        }*/
        csgo.Read(entitiesCopy[idclosestEnt].entityPtr+offsets::m_pStudioBones,&m_pStudioBones,sizeof(long));
        closestBone = hack::getClosestBone((unsigned long)m_pStudioBones, hack::bones, viewAngle, punch, myPos);
        //cout<<"closest bone "<<closestBone<<endl;
        csgo.Read((void*)m_pStudioBones+closestBone*sizeof(BoneMatrix),&theirBones,sizeof(BoneMatrix));
        theirPos.x = theirBones.x;
        theirPos.y = theirBones.y;
        theirPos.z = theirBones.z;
        if(AltTwo==5||acquiring){//if we have a target and we are still clicking alt2, or if we are in the process of moving to a target...
            /* failed at creating resolver
            float lby;
            csgo.Read(entitiesCopy[idclosestEnt].entityPtr+offsets::m_flLowerBodyYawTarget,&lby,sizeof(int));
            if(true){
                  hack::resolve(&entitiesCopy[idclosestEnt  ].entity,&theirPos,lby);
            }*/
            aimDelta=helper::calcAngle(&myPos,&theirPos);
            if(shotsFired>0){
            aimDelta.x -=viewAngle.x+(punch.x*2);
            aimDelta.y -=viewAngle.y+(punch.y*2);
            }
            else{
                aimDelta.x -=viewAngle.x;
                aimDelta.y -=viewAngle.y;
            }
            helper::clampAngle(&aimDelta);
            float xhairDistance = helper::getDistanceFov(&aimDelta,&myPos,&theirPos);
            lowestDistance=xhairDistance;
            if(lowestDistance<=fov&&lowestDistance!=-1.0){
                acquiring = true;
                if(lowestDistance<1){
                    shouldShoot=true;
                }
                newAngle=helper::calcAngle(&myPos,&theirPos);
                newAngle.x-=punch.x*2;
                newAngle.y-=punch.y*2;
                //cout<<"newAngle.x "<<newAngle.x<<" newAngle.y "<<newAngle.y<<endl;
                helper::Smoothing(&viewAngle,&newAngle, percentSmoothing);
                isAiming =true;
            }
            else{
                shouldShoot=true;
            }
        }
        if(AltTwo==4){
            isAiming=false;
        }
    }

    //rcs block
    if(!isAiming&&alwaysRCS){
        if(alwaysRCS){
            if((punchDelta.y!=0.0||punchDelta.x!=0.0)&&(shotsFired>1)){
                //seeout<<shotsFired<<endl;
                newAngle.x -=punchDelta.x*2;//new camera angle is the old camera angle minus the change in view punch angle *2 (because it works)
                newAngle.y -=punchDelta.y*2;
                newAngle.z = 0;
                //}
            }
        }
        oldPunch.y = punch.y;
        oldPunch.x = punch.x;
        oldPunch.z = 0;
    }
    if (newAngle.x!=viewAngle.x||newAngle.y!=viewAngle.y){
        hack::setVAng(&newAngle,addressOfViewAngle);
        //cout<<"set vang"<<endl;
    }
    if(foundTarget&&idclosestEnt!=0){//if we found the target and its not the world or unset
        if(AltTwo==5){
            if(shouldShoot){
                csgo.Write((void*)m_addressOfForceAttack,&toggleOn,sizeof(int));
                usleep(10000);
            }
        }
        else{
            if(!shouldShoot){
                csgo.Write((void*)m_addressOfForceAttack,&toggleOff,sizeof(int));
            }
            if(acquiring&&shouldShoot){
                csgo.Write((void*)m_addressOfForceAttack,&toggleOn,sizeof(int));
                usleep(40000);
                csgo.Write((void*)m_addressOfForceAttack,&toggleOff,sizeof(int));
                usleep(10000);
                acquiring=false;
            }
        }
    }
    else{//otherwise just shoot normally
        if(AltTwo==5){
            csgo.Write((void*)m_addressOfForceAttack,&toggleOn,sizeof(int));
            usleep(10000);
        }
        else if (AltTwo==4){
            csgo.Write((void*)m_addressOfForceAttack,&toggleOff,sizeof(int));
            isAiming = false;
        }
    }
}

int hack::getLifeState(unsigned long entityPtr){
    int lifeState = LIFE_DEAD;
    if(entityPtr!=0){
        csgo.Read((void*)entityPtr+offsets::m_lifeState,&lifeState,sizeof(char));
    }
    return lifeState;
}
int hack::getClosestBone(unsigned long m_pStudioBonesPtr, std::vector<int> &bones, QAngle &curViewAngle, QAngle &aimPunch, Vector &myPos){
    std::vector<BoneMatrix> enemyBones;
    Vector bonePos;
    QAngle aimDelta;
    for (int i = 0;i<bones.size();i++){
        //cout<<"bones iteration #"<<i<<endl;
        BoneMatrix boneMatrix;
        //cout<<"curbone "<<bones[i]<<endl;
        int curBone = bones[i];
        csgo.Read((void*)m_pStudioBonesPtr+curBone*sizeof(BoneMatrix),&boneMatrix,sizeof(BoneMatrix));
        /*cout<<boneMatrix.x<<" boneMatrix.x"<<endl;
        cout<<boneMatrix.y<<" boneMatrix.y"<<endl;
        cout<<boneMatrix.z<<" boneMatrix.z"<<endl;*/
        enemyBones.push_back(boneMatrix);
    }
    float closestDistanceToBone = -1.0;
    int closestBone = 0;
    //cout<<enemyBones.size()<<endl;
    for (int i = 0;i<enemyBones.size();i++){
        if(enemyBones[i].x==0&&enemyBones[i].y==0&&enemyBones[i].z==0){//if we find invalid data...
            continue;//skip this iteration
        }
        bonePos.x = enemyBones[i].x;
        bonePos.y = enemyBones[i].y;
        bonePos.z = enemyBones[i].z;
        aimDelta=helper::calcAngle(&myPos,&bonePos);
        aimDelta.x -=curViewAngle.x+aimPunch.x*2;//calculate aimDelta in order to calculate xhair distance
        aimDelta.y -=curViewAngle.y+aimPunch.y*2;
        helper::clampAngle(&aimDelta);
        float distanceToBone = helper::getDistanceFov(&aimDelta,&myPos,&bonePos);
        if(distanceToBone<=closestDistanceToBone || closestDistanceToBone==-1.0){
            closestDistanceToBone=distanceToBone;
            closestBone = bones[i];
        }
    }
    return closestBone;
}
void hack::setVAng(QAngle *newAngle, unsigned long addressOfViewAngle){
    helper::clampAngle(newAngle);
    csgo.Write((void*)(unsigned long)(addressOfViewAngle),&(*newAngle),sizeof(QAngle));
}
void hack::bhop(){
    unsigned int iAlt1Status = 0 ;
    int m_fFlags = 0;
    unsigned long localPlayer = 0;
    csgo.Read((void*) m_addressOfLocalPlayer, &localPlayer, sizeof(long));

    csgo.Read((void*)(m_addressOfAlt1), &iAlt1Status, sizeof(int));
    csgo.Read((void*)(localPlayer+0x138), &m_fFlags, sizeof(int));
    if(ShouldBhop==true){
        //cout<<"\nalt1: "<<iAlt1Status<<" \nonGround: "<<onGround<<endl;
        if(iAlt1Status==5&&(m_fFlags&ON_GROUND)){
            //cout<<"jumping\n:)"<<endl;
            csgo.Write((void*)((unsigned long)m_addressOfJump),&toggleOn,sizeof(int));
            this_thread::sleep_for(chrono::microseconds(500));
            csgo.Write((void*)((unsigned long)m_addressOfJump),&toggleOff, sizeof(int));
            /*XFlush(display);
            XTestFakeKeyEvent(display, 65, true, 0);
            this_thread::sleep_for(chrono::milliseconds(1));
            XTestFakeKeyEvent(display, 65, false, 0);*/
        }
    }
    else{
        if(iAlt1Status==5){
            //cout<<"jumping\n:)"<<endl;
            csgo.Write((void*)((unsigned long)m_addressOfJump),&toggleOn,sizeof(int));
            this_thread::sleep_for(chrono::microseconds(500));
        }
        else{
            csgo.Write((void*)((unsigned long)m_addressOfJump),&toggleOff, sizeof(int));
            this_thread::sleep_for(chrono::microseconds(500));
        }
    }
}
void hack::noFlash(){
    if(NoFlash){
        unsigned long localPlayer = 0;
        csgo.Read((void*) m_addressOfLocalPlayer, &localPlayer, sizeof(long));
        csgo.Write((void*)(localPlayer+offsets::m_fFlashMaxAlpha), &flashMax, sizeof(float));
    }
}
void hack::setHands(){
    if(noHands){
        int zero_ = 0;
        unsigned long localPlayer = 0;
        csgo.Read((void*) m_addressOfLocalPlayer, &localPlayer, sizeof(long));
        csgo.Write((void*)(localPlayer+0x28c), &zero_, sizeof(char));
    }
}
void hack::setFov(){
    if(viewFov==0){
        return;
    }
    int scope1 = viewFov/2;
    int scope2 = viewFov/3;
    int isScoped = 0;
    unsigned long localPlayer = 0;
    csgo.Read((void*) m_addressOfLocalPlayer, &localPlayer, sizeof(long));
    csgo.Read((void*)(localPlayer+0x4164), &isScoped, sizeof(long));
    if(isScoped==0){
        csgo.Write((void*)(localPlayer+0x3998), &viewFov, sizeof(long));
        //csgo.Write((void*)(localPlayer+0x399C), &viewFov, sizeof(float));
        //csgo.Write((void*)(localPlayer+0x3B14), &viewFov, sizeof(float));
    }
    else if(isScoped==1){
        csgo.Write((void*)(localPlayer+0x3998), &scope1, sizeof(long));
    }
    else if (isScoped==2){
        csgo.Write((void*)(localPlayer+0x3998), &scope2, sizeof(long));
    }
}
bool hack::glow(){
    int isConnected;
    csgo.Read((void*)addressIsConnected,&isConnected,sizeof(int));//+600
    if(isConnected!=1){
        return false;
    }
    struct iovec g_remote[1024], g_local[1024];
    struct hack::GlowObjectDefinition_t g_glow[1024];
    std::array<EntityInfo,64> entitiesLocal;

    // Reset
    bzero(g_remote, sizeof(g_remote));
    bzero(g_local, sizeof(g_local));
    bzero(g_glow, sizeof(g_glow));
    fill( begin(entitiesLocal), end(entitiesLocal), EntityInfo{} );

    unsigned long localPlayer = 0;
    csgo.Read((void*) m_addressOfLocalPlayer, &localPlayer, sizeof(long));

    char myLifeXD = LIFE_DEAD;
    if (localPlayer != 0){
        csgo.Read((void*)(unsigned long)localPlayer,&myLifeXD,sizeof(char));
        if(myLifeXD!=LIFE_ALIVE){
            //return false;
        }
    }
    hack::CGlowObjectManager manager;

    if (!csgo.Read((void*) m_addressOfGlowPointer, &manager, sizeof(hack::CGlowObjectManager))) {
        cout<<("Failed to read glowClassAddress")<<endl;
        throw 1;
        return false;
    }

    size_t count = manager.m_GlowObjectDefinitions.Count;
    void* data_ptr = (void*) manager.m_GlowObjectDefinitions.DataPtr;

    if (!csgo.Read(data_ptr, g_glow, sizeof(hack::GlowObjectDefinition_t) * count)) {
        cout<<("Failed to read m_GlowObjectDefinitions")<<endl;
        throw 1;
        return false;
    }

    size_t writeCount = 0;
    unsigned int teamNumber = 0;
    if (localPlayer != 0){
        csgo.Read((void*) (localPlayer+0x128), &teamNumber, sizeof(int));
        //csgo.Read((void*) (localPlayer), &(myEnt), sizeof(int));
    }
    for (unsigned int i = 0; i < count; i++) {
        if (g_glow[i].m_pEntity != NULL) {
            Entity ent;

            if (csgo.Read(g_glow[i].m_pEntity, &ent, sizeof(Entity))) {
                if (ent.m_iTeamNum != 2 && ent.m_iTeamNum != 3) {
                    g_glow[i].m_bRenderWhenOccluded = 1;
                    g_glow[i].m_bRenderWhenUnoccluded = 0;
                    continue;
                }
                entityInCrossHair = false;

                if (localPlayer != 0) {
                    if (ent.m_iTeamNum != teamNumber) {
                        unsigned int crossHairId = 0;
                        unsigned int entityId = 0;
                        csgo.Read((void*) (localPlayer+0xBBD8), &crossHairId, sizeof(int)); // 0xB390 => 0xB3A0 => 0xB398 => 0xBBD8// +0x7C avec m_bhasdefuser
                        csgo.Read((void*) (g_glow[i].m_pEntity + 0x94), &entityId, sizeof(int));

                        if (crossHairId == entityId) {
                            entityInCrossHair = true;
                        }
                    }
                }
                csgo.Write((void*) ((unsigned long) g_glow[i].m_pEntity + 0xECD), &spotted, sizeof(unsigned char));

                if (ent.m_iTeamNum == 2 || ent.m_iTeamNum == 3) {
                    if(ent.ID<=64&&ent.ID>0){
                        EntityInfo e;
                        e.entity=ent;
                        e.entityPtr=g_glow[i].m_pEntity;
                        entitiesLocal[ent.ID]=e;
                    }
                }

                if (g_glow[i].m_bRenderWhenOccluded == 1)
                    continue;

                g_glow[i].m_bRenderWhenOccluded = 1;
                g_glow[i].m_bRenderWhenUnoccluded = 0;
                //int spottedByMask = 0;
                //csgo.Read((void*)g_glow[i].m_pEntity+0xf10,&spottedByMask, sizeof(int));
                //bool spottedByMe = (spottedByMask>>myEntId-1)&1;
                if (ent.m_iTeamNum == 2 || ent.m_iTeamNum == 3) {
                    /*cout<<"ent ID " <<ent.ID<<" teamid "<<ent.m_iTeamNum;
                    cout<<" ent address " <<g_glow[i].m_pEntity<<endl;*/
                    if (ent.m_iTeamNum == 2||h.shootFriends){//&&!h.shootFriends) { //teamNumber == ent.m_iTeamNum
                        g_glow[i].m_flGlowRed = colors[0];//spottedByMe ? colors[4] : colors[0];
                        g_glow[i].m_flGlowGreen =colors[1];// spottedByMe ? colors[5] : colors[1];
                        g_glow[i].m_flGlowBlue = colors[2];//spottedByMe  ? colors[6] : colors[2];
                        g_glow[i].m_flGlowAlpha = colors[3];;//spottedByMe  ? colors[7] : colors[3];
                    } else {
                                              g_glow[i].m_flGlowRed = colors[8];
                        g_glow[i].m_flGlowGreen = colors[9];
                        g_glow[i].m_flGlowBlue = colors[10];
                        g_glow[i].m_flGlowAlpha = colors[11];
                    }
                }
            }
        }

        if (ShouldGlow) {
            size_t bytesToCutOffEnd = sizeof(hack::GlowObjectDefinition_t) - g_glow[i].writeEnd();
            size_t bytesToCutOffBegin = (size_t) g_glow[i].writeStart();
            size_t totalWriteSize = (sizeof(hack::GlowObjectDefinition_t) - (bytesToCutOffBegin + bytesToCutOffEnd));

            g_remote[writeCount].iov_base =
                    ((uint8_t*) data_ptr + (sizeof(hack::GlowObjectDefinition_t) * i)) + bytesToCutOffBegin;
            g_local[writeCount].iov_base = ((uint8_t*) &g_glow[i]) + bytesToCutOffBegin;
            g_remote[writeCount].iov_len = g_local[writeCount].iov_len = totalWriteSize;

            writeCount++;
        }
    }
    process_vm_writev(csgo.GetPid(), g_local, writeCount, g_remote, writeCount, 0);
    hack::writeEntities(entitiesLocal);
}
/*void hack::setWindowInfo(){
    WindowsMatchingPid match(hack::display,XDefaultRootWindow(hack::display),int(csgo.GetPid()));
    const list<Window> &matches = match.result();
    for(list<Window>::const_iterator it = matches.begin(); it != matches.end(); it++){
        XWindowAttributes attr;
        if (::XGetWindowAttributes(hack::display, *it, &attr))
        {
            esp::windowX = attr.x;
            esp::windowY = attr.y;
            esp::windowHeight = attr.height;
            esp::windowWidth = attr.width;
            //cout<<"esp::windowX "<<esp::windowX<<" esp::windowY "<<esp::windowY<<endl;
        }
    }
}*/
void hack::trigger(){
    if(ShouldTrigger&&entityInCrossHair){
        /*XFlush(display);
        XTestFakeButtonEvent(display, Button1, true, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        XTestFakeButtonEvent(display, Button1, false, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));*/
        unsigned int attack = 0x5;
        unsigned int release = 0x4;
        unsigned int shooting;
        csgo.Read((void*) (m_addressOfForceAttack), &shooting, sizeof(int));
        if(shooting!=attack){
            cout<<"we should be shooting"<<endl;
            csgo.Write((void*) (m_addressOfForceAttack), &attack, sizeof(int));
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            csgo.Write((void*) (m_addressOfForceAttack), &release, sizeof(int));
        }
    }
}
bool hack::checkKeys(){
    if (csgo.IsRunning()) {
        XQueryKeymap(display, keys);
        for (unsigned i = 0; i < sizeof(keys); ++i) {
            if (keys[i] != lastkeys[i]) {
                // check which key got changed
                for (unsigned j = 0, test = 1; j < 8; ++j, test *= 2) {
                    // if the key was pressed, and it wasn't before, print this
                    if ((keys[i] & test) &&
                            ((keys[i] & test) != (lastkeys[i] & test))) {
                        const int code = i * 8 + j;
                        if (code == keycodeGlow) {
                            ShouldGlow = !ShouldGlow;
                            std::cout<<"ESP: "<<ShouldGlow<<endl;
                        }
                        else if (code == keycodeTrigger){
                            ShouldTrigger = !ShouldTrigger;
                            std::cout<<"Trigger: "<<ShouldTrigger<<endl;
                        }
                        else if (code == keycodeBhop){
                            ShouldBhop = !ShouldBhop;
                            std::cout<<"Bhop: "<< ShouldBhop<<endl;
                        }
                        else if (code==keycodeNoFlash){
                            NoFlash = !NoFlash;
                            std::cout<<"NoFlash: "<<NoFlash<<endl;
                        }
                        else if (code==keycodeRCS){
                            alwaysRCS=!alwaysRCS;
                            cout<<"AlwaysRCS: "<<alwaysRCS<<endl;
                        }
                        else if (code==keycodeRage){
                            rage=!rage;
                            cout<<"RAGE: "<<rage<<endl;
                        }
                    }
                }
            }

            lastkeys[i] = keys[i];
        }
        return true;
    }
    else
    {
        cout<<"CSGO was not found. Returning."<<endl;
        return false;
    }
}
void hack::init(){
    if (getuid() != 0) {
        cout<<"Cannot start as NON ROOT user.";
        return;
    }
    hack::display = XOpenDisplay(0);
    //XInitThreads();
    //read our cfg file
    try {
        cfg.readFile("settings.cfg");
    } catch (const FileIOException &fioex) {
        cout<<"I/O error while reading settings.cfg.";
    } catch (const ParseException &pex) {
        stringstream ss;
        ss << "Parse error at " << pex.getFile() << ":" << pex.getLine() << " - " << pex.getError();
        cout<<ss.str()<<endl;
    }
    offsets::m_pStudioBones = ::strtol(helper::getConfigValue("m_pStudioBones",cfg).c_str(),NULL,0); //unsigned long long m_pStudioBones;
   offsets::m_flLowerBodyYawTarget = ::strtol(helper::getConfigValue("m_flLowerBodyYawTarget",cfg).c_str(),NULL,0); //float m_flLowerBodyYawTarget; // 0x42C8
   offsets::dwViewMatrix = ::strtol(helper::getConfigValue("m_dwViewMatrix",cfg).c_str(),NULL,0); //4x4 float matrix //2537334 and 2553C14 on Aug-7-17
   offsets::m_lifeState = 0x293; //int m_lifeState; // 0x293
   offsets::m_bIsDefusing = 0x416C;//unsigned char m_bIsDefusing; // 0x416C
   offsets::m_bIsScoped = 0x4164;//unsigned char m_bIsScoped; // 0x4164
   offsets::m_bGunGameImmunity = 0x4178;   	//unsigned char m_bGunGameImmunity; // 0x4178
   offsets::m_iShotsFired = 0xABB0;    	//int m_iShotsFired; // 0xABB0
   offsets::m_aimPunchAngle = 0x3764;    	//Vector m_aimPunchAngle; // 0x3764
    //configure our custom settings from settings.cfg
    keycodeGlow = XKeysymToKeycode(hack::display, XStringToKeysym(helper::getConfigValue("ToggleGlow",cfg).c_str()));
    cout << "ESP Toggle = " << helper::getConfigValue("ToggleGlow",cfg).c_str() << " Keycode: " << keycodeGlow << endl;
    keycodeTrigger = XKeysymToKeycode(hack::display, XStringToKeysym(helper::getConfigValue("ToggleTrigger",cfg).c_str()));
    cout << "Trigger Toggle = " << helper::getConfigValue("ToggleTrigger",cfg).c_str() << " Keycode: "<<keycodeTrigger<<endl;
    keycodeBhop = XKeysymToKeycode(hack::display, XStringToKeysym(helper::getConfigValue("ToggleBhop",cfg).c_str()));
    cout << "Bhop Toggle = " << helper::getConfigValue("ToggleBhop",cfg).c_str() <<" Keycode: " <<keycodeBhop<< endl;
    keycodeNoFlash = XKeysymToKeycode(hack::display, XStringToKeysym(helper::getConfigValue("ToggleNoFlash",cfg).c_str()));
    cout << "NoFlash Toggle = " << helper::getConfigValue("ToggleNoFlash",cfg).c_str() <<" Keycode: " <<keycodeNoFlash<< endl;
    keycodeRCS = XKeysymToKeycode(hack::display, XStringToKeysym(helper::getConfigValue("ToggleRCS",cfg).c_str()));
    cout << "RCS Toggle = " << helper::getConfigValue("ToggleRCS",cfg).c_str() <<" Keycode: " <<keycodeNoFlash<< endl;
    keycodeRage = XKeysymToKeycode(hack::display, XStringToKeysym(helper::getConfigValue("ToggleRage",cfg).c_str()));
    cout << "Rage Toggle = " << helper::getConfigValue("ToggleRage",cfg).c_str() <<" Keycode: " <<keycodeNoFlash<< endl;

    double enemyRed = ::atof(helper::getConfigValue("enemyRed",cfg).c_str());
    double enemyGreen = ::atof(helper::getConfigValue("enemyGreen",cfg).c_str());
    double enemyBlue = ::atof(helper::getConfigValue("enemyBlue",cfg).c_str());
    double enemyAlpha = ::atof(helper::getConfigValue("enemyAlpha",cfg).c_str());

    double enemyInCrosshairRed = ::atof(helper::getConfigValue("enemyInCrosshairRed",cfg).c_str());
    double enemyInCrosshairGreen = ::atof(helper::getConfigValue("enemyInCrosshairGreen",cfg).c_str());
    double enemyInCrosshairBlue = ::atof(helper::getConfigValue("enemyInCrosshairBlue",cfg).c_str());
    double enemyInCrosshairAlpha = ::atof(helper::getConfigValue("enemyInCrosshairAlpha",cfg).c_str());

    double allyRed = ::atof(helper::getConfigValue("allyRed",cfg).c_str());
    double allyGreen = ::atof(helper::getConfigValue("allyGreen",cfg).c_str());
    double allyBlue = ::atof(helper::getConfigValue("allyBlue",cfg).c_str());
    double allyAlpha = ::atof(helper::getConfigValue("allyAlpha",cfg).c_str());

    colors = new double[12] {
            enemyRed, enemyGreen, enemyBlue, enemyAlpha,
            enemyInCrosshairRed, enemyInCrosshairGreen, enemyInCrosshairBlue, enemyInCrosshairAlpha,
            allyRed, allyGreen, allyBlue, allyAlpha
};

    while (true) {
        if (remote::FindProcessByName("csgo_linux64", &csgo))
            break;
        usleep(1000);
    }
    cout<<"Discovered CSGO with PID: "<<csgo.GetPid()<<endl;

    //we will search for the client address in memory
    client.start = 0;
    engine.start = 0;

    while (client.start == 0) {
        if (!csgo.IsRunning()) {
            cout<<"The game was closed before I could find the client and engine libraries inside of csgo";
            return;
        }

        csgo.ParseMaps();

        for (auto region : csgo.regions) {
            if (region.filename.compare("client_client.so") == 0 && region.executable) {
                client = region;
            }
            if (region.filename.compare("engine_client.so") == 0 && region.executable) {
                engine = region;
            }
        }

        usleep(500);
    }

    client.client_start = client.start;
    engine.client_start = engine.start;

    //find our pointers to memory
    void* foundGlowPointerCall = client.find(csgo,
                                             "\xE8\x00\x00\x00\x00\x48\x8b\x3d\x00\x00\x00\x00\xBE\x01\x00\x00\x00\xC7", // 2017-10-06
                                             "x????xxx????xxxxxx");
    unsigned long call = csgo.GetCallAddress(foundGlowPointerCall+1);

    m_addressOfGlowPointer = csgo.GetCallAddress((void*)(call+0x10));

    unsigned long foundLocalPlayerLea = (long)client.find(csgo,
                                                          "\x48\x89\xe5\x74\x0e\x48\x8d\x05\x00\x00\x00\x00", //27/06/16
                                                          "xxxxxxxx????");

    m_addressOfLocalPlayer = csgo.GetCallAddress((void*)(foundLocalPlayerLea+8));

    unsigned long foundAttackMov = (long)client.find(csgo,
                                                     "\x44\x89\xe8\x83\xe0\x01\xf7\xd8\x83\xe8\x03\x45\x84\xe4\x74\x00\x21\xd0", //10/07/16
                                                     "xxxxxxxxxxxxxxx?xx");

    m_addressOfForceAttack = csgo.GetCallAddress((void*)(foundAttackMov+20));

    unsigned long foundAlt1Mov = (long)client.find(csgo,
                                                   "\x44\x89\xe8\xc1\xe0\x11\xc1\xf8\x1f\x83\xe8\x03\x45\x84\xe4\x74\x00\x21\xd0", //10/07/16
                                                   "xxxxxxxxxxxxxxxx?xx");

    m_addressOfAlt1 = csgo.GetCallAddress((void*)(foundAlt1Mov+21));
    m_addressOfJump = m_addressOfAlt1-0x18;
    m_addressOfAlt2 = m_addressOfAlt1+0xC;

    unsigned long foundVangMov = (long)engine.find(csgo,
                                                   "\x00\x00\x00\x00\x4a\x8b\x1c\x20\x48\x8b\x03\x48\x89\xdf\xff\x90\x00\x00\x00\x00\x41\x39\xc5", //Jul-12-17
                                                   "????xxxxxxxxxxxx????xxx");

    basePointerOfViewAngle = csgo.GetCallAddress((void*)foundVangMov);

    unsigned long foundServerDetailOffsetMov = (long)engine.find(csgo,
                                                                 "\x44\x89\xe7\x45\x89\xa5\x00\x00\x00\x00\xe8",
                                                                 "xxxxxx????x");

    int serverDetailOffset=0x294;
    csgo.Read((void*)foundServerDetailOffsetMov+21,&serverDetailOffset,sizeof(int));
    //csgo.Read((void*)serverDetailOffsetAddress,&serverDetailOffset,sizeof(int));

    unsigned long foundServerDetailBaseMove = (long)engine.find(csgo,"\x90\x0f\x1f\x84\x00\x00\x00\x00\x00\x55\x48\x89\xe5\x48\x83\xec\x00\x48\x8b\x05", "xxxx????xxxxxxxx?xxx");

    unsigned long serverDetailBase = csgo.GetCallAddress((void*)foundServerDetailBaseMove+20);

    csgo.Read((void*)serverDetailBase,&addressServerDetail,sizeof(long));

    addressServerDetail+=serverDetailOffset;

    unsigned long foundIsConnectedMov = (long)engine.find(csgo,
        "\x48\x8b\x05\x00\x00\x00\x00\x55\x40\x0f\xb6\xf7\x40\x88\x3D",
        "xxx????xxxxxxxx");
    addressIsConnected = csgo.GetCallAddress((void*)foundIsConnectedMov+15);
    
    unsigned long foundflashMaxAlphaOffset = (long)client.find(csgo, "\x5d\xc3\x66\x2e\x00\x00\x00\x00\x00\x00\x00\x00\xf3\x0f\x10\x87\x00\x00\x00\x00", "xxxx????xxxxxxxx????")+16;
    if(foundflashMaxAlphaOffset!=0){
        csgo.Read((void*)foundflashMaxAlphaOffset, (void*)offsets::m_fFlashMaxAlpha, sizeof(int));
    }
    cout<<std::hex<<"offsets::m_fFlashMaxAlpha pointer address "<<foundflashMaxAlphaOffset<<endl;
    cout<<"client.start: "<<std::hex<<client.start<<endl;
    cout<<"engine.start: "<<engine.start<<endl;
    cout<<"GlowPointer address: "<<m_addressOfGlowPointer<<endl;
    cout<<"LocalPlayer pointer address: "<< m_addressOfLocalPlayer<<endl;
    cout<<"View angle base pointer address: " << basePointerOfViewAngle<<endl;
    cout<<"server detail offset: "<<serverDetailOffset<<endl;
    cout<<"addressServerDetail: "<<addressServerDetail<<endl;
    cout<<"jump address: "<< m_addressOfJump<<endl;
    cout<<"ForceAttack address: "<< m_addressOfForceAttack<<endl;
    cout<<"Alt1 address: "<< m_addressOfAlt1<<endl;
    cout<<"Alt2 address: "<< m_addressOfAlt2<<endl;
    cout<<"addressIsConnected: "<<addressIsConnected<<endl;

    //settings
    ShouldGlow = true;
    ShouldTrigger = false;
    ShouldBhop = true;
    NoFlash = true;
    rage=false;
    alwaysRCS = false;
    bone = ::atof(helper::getConfigValue("bone",cfg).c_str());
    fov = ::atof(helper::getConfigValue("fov",cfg).c_str());
    flashMax = ::atof(helper::getConfigValue("flash_max",cfg).c_str());
    viewFov = ::atof(helper::getConfigValue("custom_fov",cfg).c_str());
    percentSmoothing = ::atof(helper::getConfigValue("aim_smooth",cfg).c_str());
    noHands =::atof(helper::getConfigValue("no_hands",cfg).c_str());
    shootFriends=::atof(helper::getConfigValue("shoot_friends",cfg).c_str());

    //check setting boundries
    if(flashMax<0||flashMax>100)
    {
        flashMax=100;
        cout<<"Flash Max setting out of bounds (0%-100%). \nSetting flashMax to 100%."<<endl;
    }
    if(percentSmoothing<0){
        percentSmoothing = .2;
        cout<<"smoothstep amount less than 0. \nSetting percentSmoothing to .20."<<endl;
    }
    if(fov<0){
        fov = 0;
    }
    cout<<"fov: "<<fov<<endl;
    //scale settings
    flashMax*=2.55;
    cout<<"flashMax: "<<std::dec<<flashMax<<"/"<<255<<endl;

    /* initialize random seed: */
    srand (time(NULL));

    spotted = 1;
    entityInCrossHair = false;
    bones.reserve(8);
    bones.push_back(6);
    bones.push_back(7);
    bones.push_back(8);
    bones.push_back(0);
}
