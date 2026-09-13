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

#include "translationsettings.h"
#include "network/translationservice.h"
#include "support/utils.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

TranslationSettings::TranslationSettings(QWidget* p)
	: QWidget(p)
{
	int spacing = Utils::layoutSpacing(this);
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);

	QLabel* label = new QLabel(tr("Cantata can send text (search terms, tooltips, artist biographies) to a local or remote "
	                              "language model to translate it. This data leaves your machine whenever translation is "
	                              "enabled, so review the provider and URL below before turning it on."),
	                           this);
	QFont f(Utils::smallFont(label->font()));
	f.setItalic(true);
	label->setFont(f);
	label->setWordWrap(true);
	mainLayout->addWidget(label);
	mainLayout->addItem(new QSpacerItem(spacing, spacing, QSizePolicy::Fixed, QSizePolicy::Fixed));

	QFormLayout* form = new QFormLayout();
	mainLayout->addLayout(form);

	enabled = new QCheckBox(tr("Enable translation"), this);
	form->addRow(enabled);

	provider = new QComboBox(this);
	provider->addItem(tr("Ollama"), QLatin1String("ollama"));
	provider->addItem(tr("OpenAI-compatible"), QLatin1String("openai"));
	form->addRow(tr("Provider:"), provider);

	url = new QLineEdit(this);
	form->addRow(tr("URL:"), url);

	model = new QLineEdit(this);
	form->addRow(tr("Model:"), model);

	apiKey = new QLineEdit(this);
	apiKey->setEchoMode(QLineEdit::Password);
	form->addRow(tr("API key:"), apiKey);

	targetLanguage = new QLineEdit(this);
	form->addRow(tr("Target language:"), targetLanguage);

	timeout = new QSpinBox(this);
	timeout->setRange(1, 900);
	timeout->setSuffix(tr(" s"));
	form->addRow(tr("Timeout:"), timeout);

	cooldown = new QSpinBox(this);
	cooldown->setRange(1, 3600);
	cooldown->setSuffix(tr(" s"));
	form->addRow(tr("Cooldown after failure:"), cooldown);

	maxConcurrent = new QSpinBox(this);
	maxConcurrent->setRange(1, 8);
	form->addRow(tr("Max concurrent requests:"), maxConcurrent);

	maxQueued = new QSpinBox(this);
	maxQueued->setRange(0, 1024);
	form->addRow(tr("Max queued requests:"), maxQueued);

	QHBoxLayout* testLayout = new QHBoxLayout();
	testButton = new QPushButton(tr("Test"), this);
	testResult = new QLineEdit(this);
	testResult->setReadOnly(true);
	testLayout->addWidget(testButton);
	testLayout->addWidget(testResult);
	form->addRow(tr("Test translation:"), testLayout);

	mainLayout->addStretch();

	connect(enabled, SIGNAL(toggled(bool)), SLOT(enabledToggled(bool)));
	connect(testButton, SIGNAL(clicked()), SLOT(testClicked()));
	connect(TranslationService::self(), &TranslationService::translationReady, this, &TranslationSettings::translationReceived);
}

void TranslationSettings::updateWidgetsEnabled()
{
	bool on = enabled->isChecked();
	provider->setEnabled(on);
	url->setEnabled(on);
	model->setEnabled(on);
	apiKey->setEnabled(on);
	targetLanguage->setEnabled(on);
	timeout->setEnabled(on);
	cooldown->setEnabled(on);
	maxConcurrent->setEnabled(on);
	maxQueued->setEnabled(on);
	testButton->setEnabled(on);
}

void TranslationSettings::enabledToggled(bool)
{
	updateWidgetsEnabled();
}

void TranslationSettings::load()
{
	QSettings settings(TranslationService::self()->configurationFilePath(), QSettings::IniFormat);
	settings.beginGroup(QLatin1String("Translation"));
	enabled->setChecked(settings.value(QLatin1String("enabled"), false).toBool());
	QString providerValue = settings.value(QLatin1String("provider"), QLatin1String("ollama")).toString().trimmed().toLower();
	int idx = provider->findData(providerValue);
	provider->setCurrentIndex(idx >= 0 ? idx : 0);
	url->setText(settings.value(QLatin1String("url")).toString());
	model->setText(settings.value(QLatin1String("model")).toString());
	apiKey->setText(settings.value(QLatin1String("apiKey")).toString());
	targetLanguage->setText(settings.value(QLatin1String("targetLanguage")).toString());
	timeout->setValue(qBound(1, settings.value(QLatin1String("timeoutMs"), 180000).toInt() / 1000, 900));
	cooldown->setValue(qBound(1, settings.value(QLatin1String("cooldownSeconds"), 30).toInt(), 3600));
	maxConcurrent->setValue(qBound(1, settings.value(QLatin1String("maxConcurrentRequests"), 1).toInt(), 8));
	maxQueued->setValue(qBound(0, settings.value(QLatin1String("maxQueuedRequests"), 16).toInt(), 1024));
	settings.endGroup();
	testResult->clear();
	updateWidgetsEnabled();
}

void TranslationSettings::save()
{
	QSettings settings(TranslationService::self()->configurationFilePath(), QSettings::IniFormat);
	settings.beginGroup(QLatin1String("Translation"));
	settings.setValue(QLatin1String("enabled"), enabled->isChecked());
	settings.setValue(QLatin1String("provider"), provider->currentData().toString());
	settings.setValue(QLatin1String("url"), url->text().trimmed());
	settings.setValue(QLatin1String("model"), model->text().trimmed());
	settings.setValue(QLatin1String("apiKey"), apiKey->text().trimmed());
	settings.setValue(QLatin1String("targetLanguage"), targetLanguage->text().trimmed());
	settings.setValue(QLatin1String("timeoutMs"), timeout->value() * 1000);
	settings.setValue(QLatin1String("cooldownSeconds"), cooldown->value());
	settings.setValue(QLatin1String("maxConcurrentRequests"), maxConcurrent->value());
	settings.setValue(QLatin1String("maxQueuedRequests"), maxQueued->value());
	settings.endGroup();
	settings.sync();
	TranslationService::self()->reloadConfiguration();
}

void TranslationSettings::testClicked()
{
	save();
	testContext = QLatin1String("settings-test-") + QString::number(QDateTime::currentMSecsSinceEpoch());
	testResult->setText(tr("Testing..."));
	testButton->setEnabled(false);
	TranslationService::self()->translate(QLatin1String("Hello, world"), testContext);
	QTimer::singleShot(timeout->value() * 1000 + 2000, this, SLOT(testTimedOut()));
}

void TranslationSettings::translationReceived(const QString& source, const QString& context, const QString& translation)
{
	Q_UNUSED(source)
	if (context != testContext || testContext.isEmpty()) {
		return;
	}
	testResult->setText(translation);
	testButton->setEnabled(enabled->isChecked());
	testContext.clear();
}

void TranslationSettings::testTimedOut()
{
	if (testContext.isEmpty()) {
		return;
	}
	testResult->setText(tr("No response / failed"));
	testButton->setEnabled(enabled->isChecked());
	testContext.clear();
}
