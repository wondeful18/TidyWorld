#include "Engine/Scene/WorldScene.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 只有进入漫游时才初始化小车，退出后恢复使用编辑器相机。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::SetDriving(bool active) {
    if (active && !mCar && !terrain) {
        SetWalking(false);
        auto& body = objects[kCarIndex].transform;
        if (spawn) {
            mParkedCar = body;
        }
        mCar.emplace(body, environment ? 8.0f : 24.0f, mParkedCar.value_or(RoamingCar::kDefaultStart));
    } else if (!active) {
        mCar.reset();
        if (mParkedCar) {
            objects[kCarIndex].transform = *mParkedCar;
            mParkedCar.reset();
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 漫游开启时把控制器结果写到与渲染器共享的车身变换。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::UpdateDrive(CarInput input, float deltaSeconds) {
    if (mCar) {
        mCar->Update(objects[kCarIndex].transform, input, deltaSeconds);
    }
}

}  // namespace tinyworld
