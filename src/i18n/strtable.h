#pragma once

#include <QString>

namespace i18n {

// 三语字符串表 (zh_cn/ja_jp/ko_kr), 数据源为内嵌 JSON 资源
// 占位符 {path} / {msg} 由调用方用 QString::replace 替换
QString t(const QString& lang, const QString& key);

} // namespace i18n
