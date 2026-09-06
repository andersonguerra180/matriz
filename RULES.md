# Project Rules

- The application supports two languages: English (EN-US, default) and Brazilian Portuguese (PT-BR, selectable in Preferences).
- All user-facing UI text, buttons, labels, and error messages must be localized through `matriz::i18n::t("key")`.
- No raw string literals should be hardcoded in user-facing UI components; all strings must exist in both `Source/Ui/Strings.h` (EN-US) and `Source/Ui/StringsPt.h` (PT-BR).
- Brazilian Portuguese (pt_BR) must strictly follow Brazilian Portuguese grammar, spelling, and audio/video preservation vocabulary (never European Portuguese).
