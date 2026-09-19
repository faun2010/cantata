/*
 * Cantata
 *
 * Copyright (c) 2026 Cantata Contributors
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

#ifndef TRANSLATION_SETTINGS_H
#define TRANSLATION_SETTINGS_H

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;

class TranslationSettings : public QWidget {
	Q_OBJECT

public:
	TranslationSettings(QWidget* p);
	~TranslationSettings() override {}

	void load();
	void save();

private Q_SLOTS:
	void enabledToggled(bool on);
	void testClicked();
	void translationReceived(const QString& source, const QString& context, const QString& translation);
	void testTimedOut();

private:
	void updateWidgetsEnabled();

private:
	QCheckBox* enabled;
	QComboBox* provider;
	QLineEdit* url;
	QLineEdit* model;
	QLineEdit* apiKey;
	QLineEdit* targetLanguage;
	QSpinBox* timeout;
	QSpinBox* cooldown;
	QSpinBox* maxConcurrent;
	QSpinBox* maxQueued;
	QPushButton* testButton;
	QLineEdit* testResult;
	QString testContext;
};

#endif
