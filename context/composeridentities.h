#ifndef COMPOSER_IDENTITIES_H
#define COMPOSER_IDENTITIES_H
#include <QJsonObject>
#include <QString>
namespace ComposerIdentities {
QString configurationPath();
// Optional override for isolated tests and portable installations; set before use.
void setConfigurationFile(const QString& path);
QString nameKey(const QString& name);
// A nonempty conflict result must never fall through to fuzzy matching.
QJsonObject lookup(const QString& name, bool* conflict = nullptr);
QString hints(const QString& text);
QString revision();
// Extract identity facts from the fetched IMSLP person header, never from LLM prose.
QJsonObject fromImslp(const QByteArray& html, const QString& category, const QString& requested);
bool mergeVerified(const QJsonObject& person, QString* error = nullptr);
}// namespace ComposerIdentities
#endif
