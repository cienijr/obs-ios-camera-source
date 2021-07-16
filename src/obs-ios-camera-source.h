/*
obs-ios-camera-source
Copyright (C) 2018 Will Townsend <will@townsend.io>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#ifndef OBSIOSCAMERASOURCE_H
#define OBSIOSCAMERASOURCE_H

#include <obs-module.h>
#include <optional>

#include "DeviceApplicationConnectionController.hpp"

#include <chrono>
#include <obs-avc.h>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "FFMpegVideoDecoder.h"
#include "FFMpegAudioDecoder.h"
#ifdef __APPLE__
#include "VideoToolboxVideoDecoder.h"
#endif

#define blog(level, msg, ...) blog(level, "[obs-ios-camera-plugin] " msg, ##__VA_ARGS__)

class IOSCameraInput : public std::enable_shared_from_this<IOSCameraInput> {
public:

	IOSCameraInput(obs_source_t *source_, obs_data_t *settings);
	~IOSCameraInput();

	void activate();
	void deactivate();
	void loadSettings(obs_data_t *settings);
	void reconnectToDevice();
	void resetDecoder();
	void connectToDevice();

    void setDeviceHostPort(std::string host, int port);

	std::shared_ptr<DeviceApplicationConnectionController> connectionController;

	obs_source_t *source;
	obs_data_t *settings;

	std::atomic_bool active = false;
	std::atomic_bool disconnectOnInactive = false;

	VideoDecoder *videoDecoder;
#ifdef __APPLE__
	VideoToolboxDecoder videoToolboxVideoDecoder;
#endif
	FFMpegVideoDecoder ffmpegVideoDecoder;
	FFMpegAudioDecoder audioDecoder;

private:
    std::optional<std::string> host;
    std::optional<int> port;

	void setupConnectionController(std::string host, int port);
};

#endif // OBSIOSCAMERASOURCE_H
