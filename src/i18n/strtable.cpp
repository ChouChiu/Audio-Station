#include "strtable.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace i18n {

namespace {

QJsonObject loadTable(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return {};
    return doc.object();
}

const QJsonObject& table(const QString& lang) {
    static const QJsonObject zhCn = loadTable(QStringLiteral(":/i18n/zh_cn.json"));
    static const QJsonObject jaJp = loadTable(QStringLiteral(":/i18n/ja_jp.json"));
    static const QJsonObject koKr = loadTable(QStringLiteral(":/i18n/ko_kr.json"));
    if (lang == QLatin1String("ja_jp"))
        return jaJp;
    if (lang == QLatin1String("ko_kr"))
        return koKr;
    return zhCn;
}

} // namespace

QString t(const QString& lang, const QString& key) {
    const QJsonObject& tbl = table(lang);
    const QJsonValue v = tbl.value(key);
    if (v.isString())
        return v.toString();
    // 缺失键回退中文, 再退化为键名本身
    const QJsonObject& zhCn = table(QStringLiteral("zh_cn"));
    const QJsonValue v2 = zhCn.value(key);
    if (v2.isString())
        return v2.toString();
    return key;
}

} // namespace i18n
