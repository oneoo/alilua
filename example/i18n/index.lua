-- i18n_load_mofile(locale, file[, domain])
i18n_load_mofile('cn', '/example/i18n/zh-Hans.mo')
-- .mo file cloud be auto reload with code-cache-ttl times
i18n_load_mofile('cn', '/example/i18n/zh-Hans2.mo')

-- i18n_set_locale(locale)
i18n_set_locale('cn')

-- __(key [, domain])
-- _e(key [, domain])
-- _n(single, plural, number [, domain])
-- _x(key, context [, domain])
-- _ex(key, context [, domain])
-- _nx(single, plural, number, context [, domain])

echo(__('Hello'))
_e('World')

i18n_set_locale('en')
echo(__('Hello'))
_e('World')
