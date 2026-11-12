## Message colors

KpOS uses fixed colors for messages so you can tell them apart at a glance.

| Message type | Color        | Number | Used for                                     |
|--------------|--------------|--------|----------------------------------------------|
| Error        | Light red    | 12     | Failures, such as a failed power-off         |
| Warning      | Yellow       | 14     | Bad input and unknown commands               |
| Usage        | Light cyan   | 11     | How a command should be used                 |
| OK           | Light green  | 10     | Success mark in `pef check`                  |
| Normal       | Set by you   | any    | Everything else, changed by `color`          |

Prompt and typed text use the colors set by `prc` and `pic`.