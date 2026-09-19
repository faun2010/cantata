/*
    SPDX-FileCopyrightText: 2023 Volker Krause <vkrause@kde.org>
    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef KNOTIFICATIONPERMISSION_H
#define KNOTIFICATIONPERMISSION_H

#include <qnamespace.h>
#include <QtGlobal>

#include <functional>

// Qt::PermissionStatus only exists since Qt 6.5; provide a minimal shim for older Qt (e.g. 6.4 on Debian bookworm / Ubuntu 24.04).
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
namespace Qt {
enum class PermissionStatus {
	Undetermined,
	Granted,
	Denied
};
}
#endif

class QObject;

/** Check or request permissions to show notifications on platforms where
 *  that is necessary.
 *
 *  @since 6.0
 */
namespace KNotificationPermission {

/** Check if the current application has permissions to show notifications. */
Qt::PermissionStatus checkPermission();

/** Request notification permissions. */
void requestPermission(QObject* context, const std::function<void(Qt::PermissionStatus)>& callback);

}// namespace KNotificationPermission

#endif
