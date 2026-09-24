# Third-Party Notices

[日本語 (JA)](#ja) | [English (EN)](#en)

## JA

RedPointには第三者のオープンソースプロジェクトに由来するコードが含まれます。該当部分には元のライセンスが適用されます。

### TinyUSB

`firmware/redpoint_pico/`のUSB descriptorおよびnetwork関連コードの一部は、TinyUSBの`net_lwip_webserver` exampleを元にしています。

Copyright (c) 2019 Ha Thach (tinyusb.org)

License: MIT

元の著作権・ライセンス表記は、対応するsource fileと`firmware/redpoint_pico/LICENSE.reference`に保持しています。

### TinyUSB net_lwip_webserver example

network glueの一部はPeter Lawrenceの実装を元にしています。

Copyright (c) 2020 Peter Lawrence

License: MIT

元のライセンス表記は`firmware/redpoint_pico/LICENSE.reference`に保持しています。

### lwIP

`firmware/redpoint_pico/src/lwipopts.h`にはlwIP由来の記述が含まれます。

License: BSD-style license

元の著作権表記とライセンス条件は、同source file内に保持しています。

### 外部依存

repositoryに同梱しない外部オープンソース依存も利用しています。

- TinyUSB
- Raspberry Pi Pico SDK — BSD-3-Clause
- Adafruit NeoPixel — LGPL-3.0-or-later

これらにはそれぞれのライセンスが適用されます。ライセンス原文は翻訳によって置き換えていません。

## EN

RedPoint includes or contains code adapted from third-party open-source
projects. Those portions remain subject to their original licenses.

### TinyUSB

Some USB descriptor and network-related code in
`firmware/redpoint_pico/` is adapted from the TinyUSB
`net_lwip_webserver` example.

Copyright (c) 2019 Ha Thach (tinyusb.org)

License: MIT

See the original copyright and license notices retained in the
corresponding source files and
`firmware/redpoint_pico/LICENSE.reference`.

### TinyUSB net_lwip_webserver example

Some network glue code is adapted from work by Peter Lawrence.

Copyright (c) 2020 Peter Lawrence

License: MIT

The original license notice is retained in
`firmware/redpoint_pico/LICENSE.reference`.

### lwIP

`firmware/redpoint_pico/src/lwipopts.h` contains material derived from lwIP.

License: BSD-style license

The original copyright notice and license terms are retained directly
in that source file.

### External dependencies

The project also uses external open-source dependencies which are not
included in this repository, including:

- TinyUSB
- Raspberry Pi Pico SDK — BSD-3-Clause
- Adafruit NeoPixel — LGPL-3.0-or-later

These dependencies remain subject to their respective licenses.
