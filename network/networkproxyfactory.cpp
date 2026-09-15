/*
 * Cantata
 *
 * Copyright (c) 2011-2022 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "networkproxyfactory.h"
#include <QHostAddress>
#include <QMutexLocker>
#include <QSettings>
#include <QStringList>
#include <stdlib.h>

const char* NetworkProxyFactory::constSettingsGroup = "Proxy";

static QList<QNetworkProxy> getSystemProxyForQuery(const QNetworkProxyQuery& query)
{
	return QNetworkProxyFactory::systemProxyForQuery(query);
}

#ifdef ENABLE_PROXY_CONFIG
NetworkProxyFactory::NetworkProxyFactory()
	: mode(Mode_System), type(QNetworkProxy::HttpProxy), port(8080)
{
	QNetworkProxyFactory::setApplicationProxyFactory(this);
	reloadSettings();
}
#else
NetworkProxyFactory::NetworkProxyFactory()
{
	QNetworkProxyFactory::setApplicationProxyFactory(this);
}
#endif

NetworkProxyFactory* NetworkProxyFactory::self()
{
	static NetworkProxyFactory* instance = nullptr;
	if (!instance) {
		instance = new NetworkProxyFactory;
	}

	return instance;
}

#ifdef ENABLE_PROXY_CONFIG
void NetworkProxyFactory::reloadSettings()
{
	QMutexLocker l(&mutex);

	QSettings s;
	s.beginGroup(constSettingsGroup);

	mode = Mode(s.value("mode", Mode_System).toInt());
	type = QNetworkProxy::ProxyType(s.value("type", QNetworkProxy::HttpProxy).toInt());
	hostname = s.value("hostname").toString();
	port = s.value("port", 8080).toInt();
	username = s.value("username").toString();
	password = s.value("password").toString();
}
#endif

static bool isLoopbackHost(const QString& host)
{
	if (0 == host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) || host.endsWith(QLatin1String(".localhost"), Qt::CaseInsensitive)) {
		return true;
	}
	QHostAddress address(host);
	return !address.isNull() && address.isLoopback();
}

QList<QNetworkProxy> NetworkProxyFactory::queryProxy(const QNetworkProxyQuery& query)
{
	// This factory is application-wide, so it also applies to the MPD
	// QTcpSocket. Never send loopback connections through a proxy.
	if (isLoopbackHost(query.peerHostName())) {
		return QList<QNetworkProxy>() << QNetworkProxy(QNetworkProxy::NoProxy);
	}

#ifdef ENABLE_PROXY_CONFIG
	QMutexLocker l(&mutex);
	QNetworkProxy ret;

	switch (mode) {
	case Mode_System:
		return getSystemProxyForQuery(query);
	case Mode_Direct:
		ret.setType(QNetworkProxy::NoProxy);
		break;
	case Mode_Manual:
		ret.setType(type);
		ret.setHostName(hostname);
		ret.setPort(port);
		if (!username.isEmpty()) {
			ret.setUser(username);
		}
		if (!password.isEmpty()) {
			ret.setPassword(password);
		}
		break;
	}

	return QList<QNetworkProxy>() << ret;
#else
	return getSystemProxyForQuery(query);
#endif
}
