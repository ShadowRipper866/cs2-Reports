## [CS 2] Report System
Плагин, который предназначен для отправки репортов игроками на сервер в дискорд
### Требования
---
- [Metamod:Source](https://www.sourcemm.net/downloads.php?branch=dev)
- [Utils](https://github.com/Pisex/cs2-menus/releases/latest)
- [AdminSystem](https://github.com/Pisex/cs2-admin_system/releases/latest)
---
### Пример конфигурации
```ini
"Config"
{
    "reasons"
    {
        "1"
        {
            "reasonText"    "Читерство"
        }
        "2"
        {
            "reasonText"    "Грифинг"
        }
        "3"
        {
            "reasonText"    "Оскорбления"
        }
        "4"
        {
            "reasonText"    "Спам"
        }
        "5"
        {
            "reasonText"    "Другое"
        }
    }
    "admin"
    {
        "immunity_flag"    "@admin/ban"  // Флаг иммунитета для админов
    }
    "debug_mode"            "0"  // 1 - включена отладка, 0 - выключена. Показывает отладочные сообщения и позволяет кидать репорты на самого себя/администраторов
    "webhook_link"          "https://discord.com/api/webhooks/..."  // Замените на ваш Discord webhook
    "enableCustomReason"    "1"  // 1 - включить кастомные причины, 0 - выключить 
    "Webhook_ServerName"    ""  // Имя сервера для webhook (опционально)
    "Webhook_color"         "#cb78ff"  // Цвет эмбеда (HEX)
    "server_ip"             "2.2.2.2:22222" (опционально)
    "webhook_description"   "Новый репорт!"  // Описание для webhook например злоебучие теги ролей и так далее
}
```
Перезагрузить конфигурация: `mm_reports_config_reload`

Названия полей/параметров можно изменить в файле `translations/reports.phrases.txt`
