#Russian

Так как Яндекс Карты для WinCE померли, сделал из ~~грязи и палок~~ MortScript и Python приложение.
Чисто для "посмотреть пробки". Основано на <a href="https://yandex.ru/dev/maps/staticapi/?from=mapsapi">StaticAPI</a> (По параметрам подгружает картинки с сервера, никакого кеширования и ведения по маршруту)

<img scr="https://GitHub.com/TpuPyku/YaMaps/YaMaps.jpg">

<h>Возможности:</h>
- Поддерживает отключение/включение пробок, переключение вида на спутник и гибрид.
- Обновляет карты раз в минуту
- Перемещает карту за вашим положением по gps. Без маршрутов и ведения по ним.

<h>Проблемы:</h>
- Тормозное :)
- Иногда загружает битую картинку map.png, просто подождите минуту или нажмите стрелки перемещения.
- Жрёт трафик, примерно до 80-200кб на каждое обновление
- Максимум 1000 запросов в день с 1 адреса к серверам Яндекса. На самом деле с большим запасом
- Основное окно сделано из SystemInformation.exe (Альтернативном лаунчере), вместе с ним может не работать, не проверял.
- С версии 1.210610 используется GPS Gate устанавливаемый в корень SDMMC, думаю будет не очень корректно работать с Гейтом из Навитела, но надо проверять, да и навителовский скрипт можно перенастроить на этот Гейт, что я и сделал себе.
- При запуске через AppLauncher трудно отловить !SYSTEMINFORMATION!YAMAPS пробуйте прописать вручную, увеличить время ожидания до 10 сек и не забыть сохранить профиль. Сам профиль можно применить YaMaps.prf, разархивировать и кинуть в ...AppLauncher/Profile
- При копировании файлов WindowsCE иногда расширение файла делает заглавными буквами. В частности файл Python25/DLLs/_socket.pyd может стать _socket.PYD, для Питона это критично. Надо проверить и остальные файлы, а исправить например в TotalCommander.
- Для работы Python 2.5 в ММС Lada Vesta не хватало библиотеки htlmview.dll

<h>Установка:</h>
- Разархивировать в корень SDMMC, чтоб папки Python25, GpsGate2.0 и YaMaps были рядышком. Запускаем YaMaps.exe
- После перезагрузки ММС и запуска YaMaps установка Питона и Гейта происходит заново (чуть дольше запуск, прописываются в реестре)
- Если что-то не так, то можно в папке YaMaps\Scripts запустить TestYaMapsDownloader.py, должно появиться пустое окно и пропасть, в папке Images обновиться файл map.png.
- Если окно не появилось значит не запускается Python,
- Если появилась ошибка Штототам "10050, 'getaddrinfo failed'", значит нет интернет подключения.

<h>В планах:</h>
- Есть возможность использовать поиск по адресам. Но тут на пути ограничения оболочки SystemInformation

<h>Обязательная юридическая информация:</h>
- Правила Яндекса https://yandex.ru/legal/maps_termsofuse/
- GpsGate and Portsplitter https://gpsgate.com (версия для WinCe больше не распространяется)
- Python 2.5 for WindowsCE https://sourceforge.net/projects/pythonce/
- MortScript http://www.sto-helit.de/index.php?module=page&entry=ms_overview&action=view&menu=29