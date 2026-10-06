/*---------------------------------------------------------*\
| R87ProPlugin.cpp                                          |
|                                                           |
|   OpenRGB plugin entry point for the Royal Kludge R87 Pro |
|                                                           |
|   The plugin does not link against OpenRGB internals. It  |
|   only uses the plugin API interface handed to Load() and |
|   registers the keyboard as a "virtual" RGB controller    |
|   whose per-LED update callbacks are implemented here.    |
|                                                           |
|   This file is part of the RK R87 Pro OpenRGB plugin      |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>

#include "R87ProPlugin.h"

#define PLUGIN_NAME     "RK R87 Pro"

/*---------------------------------------------------------*\
| Log helpers (levels match OpenRGB's LogManager enum)      |
\*---------------------------------------------------------*/
#define PLUGIN_LOG_INFO(...)    do { if(api) { api->LogEntry(__FILE__, __LINE__, 3, __VA_ARGS__); } } while(0)
#define PLUGIN_LOG_WARN(...)    do { if(api) { api->LogEntry(__FILE__, __LINE__, 2, __VA_ARGS__); } } while(0)

R87ProPlugin::R87ProPlugin()
{
}

R87ProPlugin::~R87ProPlugin()
{
    /*-----------------------------------------------------*\
    | Widgets and controllers are owned by OpenRGB; only    |
    | our own device objects are released here.             |
    \*-----------------------------------------------------*/
    for(R87ProDevice* device : devices)
    {
        delete device;
    }

    for(R87ProDongle* dongle : dongles)
    {
        delete dongle;
    }

    devices.clear();
    dongles.clear();
}

/*---------------------------------------------------------*\
| Plugin information                                        |
\*---------------------------------------------------------*/
OpenRGBPluginInfo R87ProPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;

    info.Name             = "Royal Kludge R87 Pro";
    info.Description      = "RGB control for the Royal Kludge R87 Pro keyboard (BY Tech / SinoWealth 258A:019F), over USB or its 2.4G receiver (3554:FA09).";
    info.Version          = "1.1.0";
    info.Commit           = "";
    info.URL              = "https://openrgb.org";
    info.Icon             = QImage();
    info.Location         = OPENRGB_PLUGIN_LOCATION_INFORMATION;
    info.Label            = "RK R87 Pro";
    info.TabIconString    = "";
    info.TabIcon          = QImage();
    info.ProtocolVersion  = OPENRGB_PLUGIN_API_VERSION;

    return(info);
}

unsigned int R87ProPlugin::GetPluginAPIVersion()
{
    return(OPENRGB_PLUGIN_API_VERSION);
}

/*---------------------------------------------------------*\
| Load                                                      |
\*---------------------------------------------------------*/
void R87ProPlugin::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    api = plugin_api_ptr;

    PLUGIN_LOG_INFO("[%s] plugin loaded (plugin API %d)", PLUGIN_NAME, OPENRGB_PLUGIN_API_VERSION);

    /*-----------------------------------------------------*\
    | Build the information tab                             |
    \*-----------------------------------------------------*/
    widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);

    status = new QLabel(widget);
    status->setWordWrap(true);

    QPushButton* rescan_button = new QPushButton("Rescan for RK R87 Pro", widget);

    layout->addWidget(status);
    layout->addWidget(rescan_button);
    layout->addStretch();

    QObject::connect(rescan_button, &QPushButton::clicked, [this]() { this->Rescan(); });

    hid_init();

    Rescan();
}

QWidget* R87ProPlugin::GetWidget()
{
    return(widget);
}

QMenu* R87ProPlugin::GetTrayMenu()
{
    return(nullptr);
}

/*---------------------------------------------------------*\
| Unload                                                    |
\*---------------------------------------------------------*/
void R87ProPlugin::Unload()
{
    PLUGIN_LOG_INFO("[%s] unloading", PLUGIN_NAME);

    /*-----------------------------------------------------*\
    | Stop talking to the hardware and make sure OpenRGB    |
    | does not call back into this library any more.        |
    \*-----------------------------------------------------*/
    for(R87ProDevice* device : devices)
    {
        device->Stop();
    }

    for(R87ProDongle* dongle : dongles)
    {
        dongle->Stop();
    }

    if(api != nullptr)
    {
        for(RGBControllerInterface* iface : interfaces)
        {
            /*---------------------------------------------*\
            | Unregister removes the controller from        |
            | OpenRGB's device list and clears its          |
            | callbacks, so nothing will call into this     |
            | shared library after it is unloaded.          |
            |                                               |
            | The controller object itself is deliberately  |
            | NOT deleted here: OpenRGB keeps a pointer to  |
            | it in the device page of its UI, and during   |
            | shutdown the plugin library is unloaded        |
            | (OpenRGBDialog::closeEvent ->                 |
            | PluginManager::UnloadPlugin -> dlclose)       |
            | *before* that page is destroyed.  Deleting    |
            | the controller at this point makes the page   |
            | destructor dereference a dangling pointer and |
            | OpenRGB exits with SIGSEGV.  The object is a  |
            | few kilobytes and the process is on its way   |
            | out, so leaving it alive is the safe trade.   |
            \*---------------------------------------------*/
            api->UnregisterVirtualRGBController(iface);
        }
    }

    interfaces.clear();

    for(R87ProDevice* device : devices)
    {
        delete device;
    }

    devices.clear();

    for(R87ProDongle* dongle : dongles)
    {
        delete dongle;
    }

    dongles.clear();

    hid_exit();
}

void R87ProPlugin::OnProfileAboutToLoad()
{
}

void R87ProPlugin::OnProfileLoad(nlohmann::json /*profile_data*/)
{
}

nlohmann::json R87ProPlugin::OnProfileSave()
{
    return(nullptr);
}

unsigned char* R87ProPlugin::OnSDKCommand(unsigned int /*pkt_id*/, unsigned char* /*pkt_data*/, unsigned int* /*pkt_size*/)
{
    return(nullptr);
}

void R87ProPlugin::ProfileManagerUpdated(unsigned int /*update_reason*/)
{
}

void R87ProPlugin::ResourceManagerUpdated(unsigned int /*update_reason*/)
{
}

void R87ProPlugin::SettingsManagerUpdated(unsigned int /*update_reason*/)
{
}

/*---------------------------------------------------------*\
| Enumerate the keyboard and register every device that is  |
| not registered yet                                        |
\*---------------------------------------------------------*/
void R87ProPlugin::Rescan()
{
    if(api == nullptr)
    {
        return;
    }

    std::vector<std::string> known;

    for(R87ProDevice* device : devices)
    {
        known.push_back(device->GetLocation());
    }

    hid_device_info* info_list = hid_enumerate(r87pro::VENDOR_ID, r87pro::PRODUCT_ID);

    for(hid_device_info* info = info_list; info != nullptr; info = info->next)
    {
        /*-------------------------------------------------*\
        | The keyboard exposes several collections on the   |
        | same hidraw node; only the vendor RGB collection  |
        | is interesting.                                   |
        \*-------------------------------------------------*/
        if((info->usage_page != r87pro::USAGE_PAGE) || (info->usage != r87pro::USAGE))
        {
            continue;
        }

        if(info->path == nullptr)
        {
            continue;
        }

        std::string path = info->path;

        if(std::find(known.begin(), known.end(), path) != known.end())
        {
            continue;
        }

        known.push_back(path);

        RegisterDevice(path);
    }

    if(info_list != nullptr)
    {
        hid_free_enumeration(info_list);
    }

    /*-----------------------------------------------------*\
    | The 2.4G receiver exposes the same keyboard through   |
    | its own tunnel.  Prefer the wired path when the cable |
    | is plugged in, otherwise fall back to the receiver.   |
    \*-----------------------------------------------------*/
    if(devices.empty())
    {
        hid_device_info* dongle_list = hid_enumerate(r87pro::DONGLE_VENDOR_ID, r87pro::DONGLE_PRODUCT_ID);

        for(hid_device_info* info = dongle_list; info != nullptr; info = info->next)
        {
            if((info->usage_page != r87pro::DONGLE_USAGE_PAGE) || (info->usage != r87pro::DONGLE_USAGE))
            {
                continue;
            }

            if(info->path == nullptr)
            {
                continue;
            }

            std::string path = info->path;

            if(std::find(known.begin(), known.end(), path) != known.end())
            {
                continue;
            }

            known.push_back(path);

            RegisterDongle(path);
        }

        if(dongle_list != nullptr)
        {
            hid_free_enumeration(dongle_list);
        }
    }

    UpdateStatusLabel();
}

void R87ProPlugin::RegisterDevice(const std::string& path)
{
    hid_device* dev = hid_open_path(path.c_str());

    if(dev == nullptr)
    {
        last_error = QString::fromStdString(path);
        PLUGIN_LOG_WARN("[%s] could not open %s - install the udev rule from the plugin's udev/ directory", PLUGIN_NAME, path.c_str());
        return;
    }

    unsigned char model_id = R87ProQueryModelID(dev);

    PLUGIN_LOG_INFO("[%s] device at %s reports model ID 0x%02X (%s)", PLUGIN_NAME, path.c_str(), model_id,
                    r87pro::ModelName(model_id).c_str());

    /*-----------------------------------------------------*\
    | Layout: default TKL layout, optionally overridden by  |
    | a JSON file in the OpenRGB configuration directory    |
    \*-----------------------------------------------------*/
    r87pro::Layout layout = r87pro::GetDefaultTKL();

    std::string override_path = (api->GetConfigurationDirectory() / "rk-r87pro-layout.json").string();
    std::string override_error;

    if(layout.ApplyJsonOverrides(override_path, override_error))
    {
        PLUGIN_LOG_INFO("[%s] applied layout overrides from %s", PLUGIN_NAME, override_path.c_str());
    }

    if(!override_error.empty())
    {
        PLUGIN_LOG_WARN("[%s] layout override: %s", PLUGIN_NAME, override_error.c_str());
    }

    std::string device_name = "Royal Kludge R87 Pro";
    R87ProDevice* device = new R87ProDevice(dev, path, device_name, model_id, layout);

    /*-----------------------------------------------------*\
    | Controller description                                |
    \*-----------------------------------------------------*/
    RGBController_Setup setup;

    setup.name        = device_name;
    setup.vendor      = "Royal Kludge";
    setup.description = "RK R87 Pro (BY Tech / SinoWealth)";
    setup.version     = "1.0";
    setup.serial      = "";
    setup.location    = "HID: " + path;
    setup.type        = DEVICE_TYPE_KEYBOARD;
    setup.flags       = 0;
    setup.active_mode = r87pro::MODE_DIRECT;

    mode off_mode;
    off_mode.name       = "Off";
    off_mode.value      = r87pro::MODE_OFF;
    off_mode.flags      = 0;
    off_mode.color_mode = MODE_COLORS_NONE;

    mode direct_mode;
    direct_mode.name       = "Direct";
    direct_mode.value      = r87pro::MODE_DIRECT;
    direct_mode.flags      = MODE_FLAG_HAS_PER_LED_COLOR;
    direct_mode.color_mode = MODE_COLORS_PER_LED;

    setup.modes.push_back(off_mode);
    setup.modes.push_back(direct_mode);

    /*-----------------------------------------------------*\
    | LEDs (one per hardware LED slot)                      |
    \*-----------------------------------------------------*/
    setup.leds.resize(layout.led_count);

    for(unsigned int led_idx = 0; led_idx < layout.led_count; led_idx++)
    {
        setup.leds[led_idx].name  = "LED " + std::to_string(led_idx);
        setup.leds[led_idx].value = led_idx;
    }

    for(const r87pro::KeyEntry& key : layout.keys)
    {
        if(key.led < setup.leds.size())
        {
            setup.leds[key.led].name = key.name;
        }
    }

    /*-----------------------------------------------------*\
    | Keyboard zone with the key matrix                     |
    \*-----------------------------------------------------*/
    std::vector<unsigned int> matrix_map = layout.BuildMatrixMap();

    zone keyboard_zone;
    keyboard_zone.name         = "Keyboard";
    keyboard_zone.display_name = "Keyboard";
    keyboard_zone.type         = ZONE_TYPE_MATRIX;
    keyboard_zone.leds_count   = layout.led_count;
    keyboard_zone.leds_min     = layout.led_count;
    keyboard_zone.leds_max     = layout.led_count;
    keyboard_zone.flags        = 0;
    keyboard_zone.matrix_map.Set(layout.height, layout.width, matrix_map.data());

    setup.zones.push_back(keyboard_zone);

    /*-----------------------------------------------------*\
    | Device callbacks                                      |
    \*-----------------------------------------------------*/
    setup.object_ptr                                  = device;
    setup.DeviceUpdateLEDs                            = &R87ProDevice::UpdateLEDsCallback;
    setup.DeviceUpdateMode                            = &R87ProDevice::UpdateModeCallback;
    setup.DeviceConfigureZone                         = &R87ProDevice::ConfigureZoneCallback;
    setup.DeviceUpdateZoneLEDs                        = nullptr;
    setup.DeviceUpdateSingleLED                       = nullptr;
    setup.DeviceSaveMode                              = nullptr;
    setup.DeviceUpdateZoneMode                        = nullptr;
    setup.DeviceUpdateDeviceSpecificConfiguration     = nullptr;
    setup.DeviceUpdateDeviceSpecificZoneConfiguration = nullptr;

    RGBControllerInterface* iface = api->CreateVirtualRGBController(&setup);

    if(iface == nullptr)
    {
        PLUGIN_LOG_WARN("[%s] OpenRGB refused to create the virtual controller", PLUGIN_NAME);
        delete device;
        return;
    }

    device->SetControllerInterface(iface);
    api->RegisterVirtualRGBController(iface);

    PLUGIN_LOG_INFO("[%s] OpenRGB reports %u controllers after registration", PLUGIN_NAME,
                    (unsigned int)api->GetRGBControllers().size());

    device->Start();

    devices.push_back(device);
    interfaces.push_back(iface);

    PLUGIN_LOG_INFO("[%s] registered %s (%u LEDs) at %s", PLUGIN_NAME, device_name.c_str(), layout.led_count, path.c_str());
}

/*---------------------------------------------------------*\
| The 2.4G receiver: same keyboard, different transport.    |
| Its vendor collection is usage page 0xFF02 usage 2 and    |
| it answers whether a keyboard is linked; only register    |
| it when one is.                                           |
\*---------------------------------------------------------*/
void R87ProPlugin::RegisterDongle(const std::string& path)
{
    hid_device* dev = hid_open_path(path.c_str());

    if(dev == nullptr)
    {
        PLUGIN_LOG_WARN("[%s] could not open the 2.4G receiver at %s - install the udev rule from the plugin's udev/ directory", PLUGIN_NAME, path.c_str());
        return;
    }

    /*-----------------------------------------------------*\
    | Same LED layout as the wired path, same override file |
    \*-----------------------------------------------------*/
    r87pro::Layout layout = r87pro::GetDefaultTKL();

    std::string override_path = (api->GetConfigurationDirectory() / "rk-r87pro-layout.json").string();
    std::string override_error;

    if(layout.ApplyJsonOverrides(override_path, override_error))
    {
        PLUGIN_LOG_INFO("[%s] applied layout overrides from %s", PLUGIN_NAME, override_path.c_str());
    }

    if(!override_error.empty())
    {
        PLUGIN_LOG_WARN("[%s] layout override: %s", PLUGIN_NAME, override_error.c_str());
    }

    std::string device_name = "Royal Kludge R87 Pro (2.4G)";

    R87ProDongle* dongle = new R87ProDongle(dev, path, device_name, layout);

    if(!dongle->Probe())
    {
        PLUGIN_LOG_INFO("[%s] 2.4G receiver at %s answers but no keyboard is linked to it", PLUGIN_NAME, path.c_str());
        delete dongle;
        return;
    }

    PLUGIN_LOG_INFO("[%s] 2.4G receiver at %s: model ID 0x%02X (%s), firmware version %s", PLUGIN_NAME, path.c_str(),
                    dongle->GetModelID(), r87pro::ModelName(dongle->GetModelID()).c_str(),
                    dongle->GetFirmwareVersion().c_str());

    /*-----------------------------------------------------*\
    | Controller description                                |
    \*-----------------------------------------------------*/
    RGBController_Setup setup;

    setup.name        = device_name;
    setup.vendor      = "Royal Kludge";
    setup.description = "RK R87 Pro (BY Tech / SinoWealth) over the 2.4G receiver";
    setup.version     = "1.0";
    setup.serial      = "";
    setup.location    = "HID: " + path + " (2.4G)";
    setup.type        = DEVICE_TYPE_KEYBOARD;
    setup.flags       = 0;
    setup.active_mode = r87pro::DONGLE_MODE_DIRECT;

    mode off_mode;
    off_mode.name       = "Off";
    off_mode.value      = r87pro::DONGLE_MODE_OFF;
    off_mode.flags      = 0;
    off_mode.color_mode = MODE_COLORS_NONE;

    mode direct_mode;
    direct_mode.name       = "Direct";
    direct_mode.value      = r87pro::DONGLE_MODE_DIRECT;
    direct_mode.flags      = MODE_FLAG_HAS_PER_LED_COLOR;
    direct_mode.color_mode = MODE_COLORS_PER_LED;

    setup.modes.push_back(off_mode);
    setup.modes.push_back(direct_mode);

    /*-----------------------------------------------------*\
    | LEDs (one per hardware LED slot)                      |
    \*-----------------------------------------------------*/
    setup.leds.resize(layout.led_count);

    for(unsigned int led_idx = 0; led_idx < layout.led_count; led_idx++)
    {
        setup.leds[led_idx].name  = "LED " + std::to_string(led_idx);
        setup.leds[led_idx].value = led_idx;
    }

    for(const r87pro::KeyEntry& key : layout.keys)
    {
        if(key.led < setup.leds.size())
        {
            setup.leds[key.led].name = key.name;
        }
    }

    /*-----------------------------------------------------*\
    | Keyboard zone with the key matrix                     |
    \*-----------------------------------------------------*/
    std::vector<unsigned int> matrix_map = layout.BuildMatrixMap();

    zone keyboard_zone;
    keyboard_zone.name         = "Keyboard";
    keyboard_zone.display_name = "Keyboard";
    keyboard_zone.type         = ZONE_TYPE_MATRIX;
    keyboard_zone.leds_count   = layout.led_count;
    keyboard_zone.leds_min     = layout.led_count;
    keyboard_zone.leds_max     = layout.led_count;
    keyboard_zone.flags        = 0;
    keyboard_zone.matrix_map.Set(layout.height, layout.width, matrix_map.data());

    setup.zones.push_back(keyboard_zone);

    /*-----------------------------------------------------*\
    | Device callbacks                                      |
    \*-----------------------------------------------------*/
    setup.object_ptr                                  = dongle;
    setup.DeviceUpdateLEDs                            = &R87ProDongle::UpdateLEDsCallback;
    setup.DeviceUpdateMode                            = &R87ProDongle::UpdateModeCallback;
    setup.DeviceConfigureZone                         = &R87ProDongle::ConfigureZoneCallback;
    setup.DeviceUpdateZoneLEDs                        = nullptr;
    setup.DeviceUpdateSingleLED                       = nullptr;
    setup.DeviceSaveMode                              = nullptr;
    setup.DeviceUpdateZoneMode                        = nullptr;
    setup.DeviceUpdateDeviceSpecificConfiguration     = nullptr;
    setup.DeviceUpdateDeviceSpecificZoneConfiguration = nullptr;

    RGBControllerInterface* iface = api->CreateVirtualRGBController(&setup);

    if(iface == nullptr)
    {
        PLUGIN_LOG_WARN("[%s] OpenRGB refused to create the 2.4G virtual controller", PLUGIN_NAME);
        delete dongle;
        return;
    }

    dongle->SetControllerInterface(iface);
    api->RegisterVirtualRGBController(iface);

    dongle->Start();

    dongles.push_back(dongle);
    interfaces.push_back(iface);

    PLUGIN_LOG_INFO("[%s] registered %s (%u LEDs) at %s", PLUGIN_NAME, device_name.c_str(), layout.led_count, path.c_str());
}

void R87ProPlugin::UpdateStatusLabel()
{
    if(status == nullptr)
    {
        return;
    }

    QString text;

    text += "<b>Royal Kludge R87 Pro</b><br>";
    text += "USB: 258A:019F (usage page 0xFF00) &nbsp;|&nbsp; 2.4G receiver: 3554:FA09 (usage page 0xFF02)<br><br>";

    if(devices.empty() && dongles.empty())
    {
        text += "No keyboard registered.<br>";
        text += "If the keyboard is connected (USB or through its 2.4G receiver), install the udev rules "
                "shipped in the plugin's <code>udev/</code> directory - the stock OpenRGB rules cover "
                "neither 258A:019F nor 3554:FA09 - then press <i>Rescan</i>.";
    }
    else
    {
        for(R87ProDevice* device : devices)
        {
            text += QString("<b>%1</b><br>").arg(QString::fromStdString(device->GetName()));
            text += QString("Model ID: 0x%1 &nbsp; LEDs: %2<br>")
                        .arg(device->GetModelID(), 2, 16, QChar('0'))
                        .arg(device->GetLEDCount());
            text += QString("Interface: %1<br><br>").arg(QString::fromStdString(device->GetLocation()));
        }

        for(R87ProDongle* dongle : dongles)
        {
            text += QString("<b>%1</b><br>").arg(QString::fromStdString(dongle->GetName()));
            text += QString("Model ID: 0x%1 &nbsp; Firmware: %2 &nbsp; LEDs: %3<br>")
                        .arg(dongle->GetModelID(), 2, 16, QChar('0'))
                        .arg(QString::fromStdString(dongle->GetFirmwareVersion()))
                        .arg(dongle->GetLEDCount());
            text += QString("Interface: %1<br><br>").arg(QString::fromStdString(dongle->GetLocation()));
        }
    }

    status->setText(text);
}
