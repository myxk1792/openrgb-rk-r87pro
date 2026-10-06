/*---------------------------------------------------------*\
| R87ProPlugin.h                                            |
|                                                           |
|   OpenRGB plugin entry point for the Royal Kludge R87 Pro |
|                                                           |
|   This file is part of the RK R87 Pro OpenRGB plugin      |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QWidget>
#include <QLabel>
#include <QString>
#include <string>
#include <vector>

#include "OpenRGBPluginInterface.h"
#include "R87ProDevice.h"

class R87ProPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "metadata.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    R87ProPlugin();
    ~R87ProPlugin();

    /*-----------------------------------------------------*\
    | OpenRGBPluginInterface                                |
    \*-----------------------------------------------------*/
    OpenRGBPluginInfo   GetPluginInfo()                                                                         override;
    unsigned int        GetPluginAPIVersion()                                                                   override;

    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr)                                         override;
    QWidget*            GetWidget()                                                                             override;
    QMenu*              GetTrayMenu()                                                                           override;
    void                Unload()                                                                                override;
    void                OnProfileAboutToLoad()                                                                  override;
    void                OnProfileLoad(nlohmann::json profile_data)                                              override;
    nlohmann::json      OnProfileSave()                                                                         override;
    unsigned char*      OnSDKCommand(unsigned int pkt_id, unsigned char * pkt_data, unsigned int *pkt_size)     override;
    void                ProfileManagerUpdated(unsigned int update_reason)                                        override;
    void                ResourceManagerUpdated(unsigned int update_reason)                                      override;
    void                SettingsManagerUpdated(unsigned int update_reason)                                      override;

    /*-----------------------------------------------------*\
    | Device detection / registration                       |
    \*-----------------------------------------------------*/
    void                Rescan();

private:
    void                RegisterDevice(const std::string& path);
    void                UpdateStatusLabel();

    OpenRGBPluginAPIInterface*              api = nullptr;
    std::vector<R87ProDevice*>              devices;
    std::vector<RGBControllerInterface*>    interfaces;
    QWidget*                                widget   = nullptr;
    QLabel*                                 status   = nullptr;
    QString                                 last_error;
};
