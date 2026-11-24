#include "Engine/Scene/DayCycle.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// fmod 允许步进越过一天边界，负值移动回同一周期。
//————————————————————————————————————————————————————————————————————————————————
float WrapDayHour(float hour) {
    const float wrapped = std::fmod(hour, 24.0f);
    return wrapped < 0.0f ? wrapped + 24.0f : wrapped;
}

//————————————————————————————————————————————————————————————————————————————————
// 6:00 和 18:00 为地平线时刻。直射到零之后，天空仍有连续的暮光。
//————————————————————————————————————————————————————————————————————————————————
Daylight EvaluateDaylight(float hour) {
    const float hoursFromDawn = WrapDayHour(hour) - 6.0f;
    const float height = std::sin(hoursFromDawn * (glm::two_pi<float>() / 24.0f));
    const float daylight = glm::smoothstep(0.0f, 0.33f, height);
    const float night = 1.0f - glm::smoothstep(-0.22f, 0.35f, height);
    DirectionalSun sun;
    sun.azimuth = std::remainder(90.0f + 15.0f * hoursFromDawn, 360.0f);
    sun.elevation = 64.0f * std::max(height, 0.0f);
    sun.intensity = daylight * std::lerp(0.88f, 1.20f, glm::smoothstep(0.25f, 0.90f, height));
    sun.color = glm::mix(glm::vec3{1.0f, 0.47f, 0.22f}, glm::vec3{1.0f, 0.96f, 0.88f},
                         glm::smoothstep(0.14f, 0.66f, height));
    return {sun, std::lerp(1.0f, 0.12f, night), night};
}

}  // namespace tinyworld
