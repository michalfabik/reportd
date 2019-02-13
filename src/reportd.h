/* reportd -- Software problem reporting service
 *
 * Copyright 2016 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Jakub Filak <jfilak@redhat.com>
 */

#pragma once

#include "reportd-daemon.h"
#include "reportd-service.h"
#include "reportd-task.h"

#define REPORTD_DBUS_BUS_NAME            "org.freedesktop.reportd"
#define REPORTD_DBUS_OBJECT_MANAGER_PATH "/org/freedesktop/reportd"
#define REPORTD_DBUS_SERVICE_PATH        "/org/freedesktop/reportd/Service"
#define REPORTD_DBUS_TASK_PATH           "/org/freedesktop/reportd/Task"
#define REPORTD_DBUS_TASK_PROMPT_PATH    REPORTD_DBUS_TASK_PATH "/Prompt"
