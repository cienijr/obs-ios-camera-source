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

#include "obs-ios-camera-source.h"

#define TEXT_INPUT_NAME obs_module_text("OBSIOSCamera.Title")
#define SETTING_DEVICE_HOST "setting_device_host"
#define SETTING_DEVICE_PORT "setting_device_port"
#define SETTING_PROP_LATENCY "latency"
#define SETTING_PROP_LATENCY_NORMAL 0
#define SETTING_PROP_LATENCY_LOW 1
#define SETTING_PROP_HARDWARE_DECODER "setting_use_hw_decoder"
#define SETTING_PROP_DISCONNECT_ON_INACTIVE "setting_disconnect_on_inactive"
#define SETTING_PROP_FFMPEG_HARDWARE_DECODER "setting_use_ffmpeg_hw_decoder"

IOSCameraInput::IOSCameraInput(obs_source_t *source_, obs_data_t *settings)
	: source(source_), settings(settings)
{
	blog(LOG_INFO, "Creating instance of plugin!");

#ifdef __APPLE__
	videoToolboxVideoDecoder.source = source_;
	videoToolboxVideoDecoder.Init();
#endif

	ffmpegVideoDecoder.source = source_;
	ffmpegVideoDecoder.Init();

	audioDecoder.source = source_;
	audioDecoder.Init();

	videoDecoder = &ffmpegVideoDecoder;

	active = true;
	loadSettings(settings);
};

void IOSCameraInput::setupConnectionController(std::string host, int port)
{
	std::cout << "Did Add device " << host << ":" << port << std::endl;

	// Create the connection, and the connection manager, but don't start anything just yet
	auto deviceConnection = std::make_shared<portal::DeviceConnection>(host, port);
	auto deviceConnectionController = std::make_shared<DeviceApplicationConnectionController>(deviceConnection);

	connectionController = deviceConnectionController;

	// Setup the callbacks

	deviceConnectionController->onProcessPacketCallback = [this](auto packet) {
		try {
			switch (packet.type) {
			case 101: // Video Packet
				this->videoDecoder->Input(packet.data, packet.type, packet.tag);
				break;
			case 102: // Audio Packet
				this->audioDecoder.Input(packet.data, packet.type, packet.tag);
			default:
				break;
			}
		} catch (...) {
			// This isn't great, but I haven't been able to figure out what is causing
			// the exception that happens when
			//   the phone is plugged in with the app open
			//   OBS Studio is launched with the iOS Camera plugin ready
			// This also doesn't happen _all_ the time. Which makes this 'fun'..
			blog(LOG_INFO, "Exception caught...");
		}
	};

	resetDecoder();
}

IOSCameraInput ::~IOSCameraInput()
{

}

void IOSCameraInput::activate()
{
	blog(LOG_INFO, "Activating");
	active = true;

	connectToDevice();
}

void IOSCameraInput::deactivate()
{
	blog(LOG_INFO, "Deactivating");
	active = false;

	connectToDevice();
}

void IOSCameraInput::loadSettings(obs_data_t *settings)
{
	disconnectOnInactive = obs_data_get_bool(
		settings, SETTING_PROP_DISCONNECT_ON_INACTIVE);

	auto device_host = obs_data_get_string(settings, SETTING_DEVICE_HOST);
	auto device_port = obs_data_get_int(settings, SETTING_DEVICE_PORT);

	blog(LOG_INFO, "Loaded Settings");

	setDeviceHostPort(device_host, device_port);
}

void IOSCameraInput::setDeviceHostPort(std::string host, int port)
{
    this->host = host;
    this->port = port;
    connectToDevice();
}

void IOSCameraInput::reconnectToDevice()
{
	connectToDevice();
}

void IOSCameraInput::resetDecoder()
{
	// flush the decoders
	ffmpegVideoDecoder.Flush();
#ifdef __APPLE__
	videoToolboxVideoDecoder.Flush();
#endif

	// Clear the video frame when a setting changes
	obs_source_output_video(source, NULL);
}

void IOSCameraInput::connectToDevice()
{
    auto host = this->host.value_or("");
    auto port = this->port.value_or(0);

	// If there is no currently selected device, disconnect from all
	// connection controllers
	if (host.empty() || port <= 0) {
	    if (connectionController != nullptr) {
	        connectionController->disconnect();
	        connectionController = nullptr;
		}

		// Clear the video frame when a setting changes
		resetDecoder();
		return;
	}

    blog(LOG_DEBUG, "Connecting to %s:%d", host.c_str(), port);

	if (connectionController != nullptr) {
	    if (connectionController->getHost() != host || connectionController->getPort() != port) {
	        // connection changed
	        connectionController->disconnect();
            setupConnectionController(host, port);
	    }
	} else {
        setupConnectionController(host, port);
	}

	auto shouldConnect = !disconnectOnInactive || active;

	// Then connect to the selected device if the plugin is active, or inactive and connected on inactive.
	if (shouldConnect) {
	    if (connectionController != nullptr) {
            blog(LOG_DEBUG, "Starting connection controller");
            connectionController->start();
//            connectionController->start(host, port);
	    }
	}
}

#pragma mark - Settings Config

static bool reconnect_to_device(obs_properties_t *props, obs_property_t *p,
				void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);

	auto cameraInput = reinterpret_cast<IOSCameraInput *>(data);
	cameraInput->reconnectToDevice();

	return false;
}

#pragma mark - Plugin Callbacks

static const char *GetIOSCameraInputName(void *)
{
	return TEXT_INPUT_NAME;
}

static void UpdateIOSCameraInput(void *data, obs_data_t *settings);

static void *CreateIOSCameraInput(obs_data_t *settings, obs_source_t *source)
{
	IOSCameraInput *cameraInput = nullptr;

    try
    {
        cameraInput = new IOSCameraInput(source, settings);
        UpdateIOSCameraInput(cameraInput, settings);
    }
    catch (const char *error)
    {
        blog(LOG_ERROR, "Could not create device '%s': %s", obs_source_get_name(source), error);
    }

	return cameraInput;
}

static void DestroyIOSCameraInput(void *data)
{
	delete reinterpret_cast<IOSCameraInput *>(data);
}

static void DeactivateIOSCameraInput(void *data)
{
	auto cameraInput = reinterpret_cast<IOSCameraInput *>(data);
	cameraInput->deactivate();
}

static void ActivateIOSCameraInput(void *data)
{
	auto cameraInput = reinterpret_cast<IOSCameraInput *>(data);
	cameraInput->activate();
}

static obs_properties_t *GetIOSCameraProperties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *ppts = obs_properties_create();

    obs_properties_add_text(
            ppts, SETTING_DEVICE_HOST,
            obs_module_text("OBSIOSCamera.Settings.Device.Host"),
            OBS_TEXT_DEFAULT);
    obs_properties_add_int(
            ppts, SETTING_DEVICE_PORT,
            obs_module_text("OBSIOSCamera.Settings.Device.Port"),
            0, 65535,
            1);

	obs_properties_add_button(ppts, "setting_button_connect_to_device",
				  "Reconnect to Device", reconnect_to_device);

	obs_property_t *latency_modes = obs_properties_add_list(
		ppts, SETTING_PROP_LATENCY,
		obs_module_text("OBSIOSCamera.Settings.Latency"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(
		latency_modes,
		obs_module_text("OBSIOSCamera.Settings.Latency.Normal"),
		SETTING_PROP_LATENCY_NORMAL);
	obs_property_list_add_int(
		latency_modes,
		obs_module_text("OBSIOSCamera.Settings.Latency.Low"),
		SETTING_PROP_LATENCY_LOW);

#ifdef __APPLE__
	obs_properties_add_bool(
		ppts, SETTING_PROP_HARDWARE_DECODER,
		obs_module_text("OBSIOSCamera.Settings.UseHardwareDecoder"));
#endif

	obs_properties_add_bool(
		ppts, SETTING_PROP_DISCONNECT_ON_INACTIVE,
		obs_module_text("OBSIOSCamera.Settings.DisconnectOnInactive"));

    obs_properties_add_bool(
            ppts, SETTING_PROP_FFMPEG_HARDWARE_DECODER,
            obs_module_text("OBSIOSCamera.Settings.UseFFMpegHardwareDecoder"));

	return ppts;
}

static void GetIOSCameraDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SETTING_DEVICE_HOST, "");
	obs_data_set_default_int(settings, SETTING_DEVICE_PORT, 2019);

	obs_data_set_default_int(settings, SETTING_PROP_LATENCY,
				 SETTING_PROP_LATENCY_LOW);
#ifdef __APPLE__
	obs_data_set_default_bool(settings, SETTING_PROP_HARDWARE_DECODER,
				  false);
#endif
	obs_data_set_default_bool(settings, SETTING_PROP_DISCONNECT_ON_INACTIVE,
				  false);
    obs_data_set_default_bool(settings, SETTING_PROP_FFMPEG_HARDWARE_DECODER, false);
}

static void SaveIOSCameraInput(void *data, obs_data_t *settings)
{
    IOSCameraInput *input = reinterpret_cast<IOSCameraInput *>(data);

    // Connect to the device
	auto deviceHost = obs_data_get_string(settings, SETTING_DEVICE_HOST);
	auto devicePort = obs_data_get_int(settings, SETTING_DEVICE_PORT);
	input->setDeviceHostPort(deviceHost, devicePort);
}

static void UpdateIOSCameraInput(void *data, obs_data_t *settings)
{
	IOSCameraInput *input = reinterpret_cast<IOSCameraInput *>(data);

    // Connect to the device
//	auto deviceHost = obs_data_get_string(settings, SETTING_DEVICE_HOST);
//	auto devicePort = obs_data_get_int(settings, SETTING_DEVICE_PORT);
//	input->setDeviceHostPort(deviceHost, devicePort);

	const bool is_unbuffered =
		(obs_data_get_int(settings, SETTING_PROP_LATENCY) ==
		 SETTING_PROP_LATENCY_LOW);
	obs_source_set_async_unbuffered(input->source, is_unbuffered);

	bool useFFMpegHardwareDecoder =
        obs_data_get_bool(settings, SETTING_PROP_FFMPEG_HARDWARE_DECODER);

	input->ffmpegVideoDecoder.setHW(useFFMpegHardwareDecoder);

#ifdef __APPLE__
	bool useHardwareDecoder =
		obs_data_get_bool(settings, SETTING_PROP_HARDWARE_DECODER);

	if (useHardwareDecoder && !useFFMpegHardwareDecoder) {
		input->videoDecoder = &input->videoToolboxVideoDecoder;
	} else {
		input->videoDecoder = &input->ffmpegVideoDecoder;
	}
#endif

	input->disconnectOnInactive = obs_data_get_bool(
		settings, SETTING_PROP_DISCONNECT_ON_INACTIVE);
}

void RegisterIOSCameraSource()
{
	obs_source_info info = {};
	info.id = "ios-camera-source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
	info.get_name = GetIOSCameraInputName;

	info.create = CreateIOSCameraInput;
	info.destroy = DestroyIOSCameraInput;

	info.deactivate = DeactivateIOSCameraInput;
	info.activate = ActivateIOSCameraInput;

	info.get_defaults = GetIOSCameraDefaults;
	info.get_properties = GetIOSCameraProperties;
	info.save = SaveIOSCameraInput;
	info.update = UpdateIOSCameraInput;
	info.icon_type = OBS_ICON_TYPE_CAMERA;
	obs_register_source(&info);
}
