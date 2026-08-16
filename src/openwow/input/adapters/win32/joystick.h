#pragma once

#include <cstdint>

namespace openwow::input {

int W32Joystick_EnumObjectsCallback(const void* doi, void* device);

void W32Joystick_SetStickAxes(uint32_t device_index, uint32_t stick_index,
                              uint32_t axis_x_index, uint32_t axis_y_index);

void W32Joystick_EnableSliderAxis(uint32_t device_index, uint32_t axis_index);

int W32Joystick_ResetState(void* device, int32_t* state);

void* W32JoystickArray_SetCapacity(void* array_header, uint32_t new_capacity);

void W32Joystick_ReleaseDevice(uint32_t device_index);

void W32Joystick_SetFreeLookMode(uint32_t device_index, uint32_t mode);

}
