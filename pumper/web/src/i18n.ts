import traditionalChinese from "./locales/zh-TW.json";

export type Locale = "en" | "zh-TW";
export type Message = keyof typeof traditionalChinese;

type Placeholders<S extends string> = S extends `${string}{${infer Name}}${infer Rest}`
  ? Name | Placeholders<Rest> : never;
type Arguments<K extends Message> = [Placeholders<K>] extends [never]
  ? [values?: never] : [values: Record<Placeholders<K>, string | number>];

/** Match browser preferences in order; never persist a separate app language. */
export function resolveLocale(languages: readonly string[]): Locale {
  for (const language of languages) {
    try {
      const candidate = new Intl.Locale(language);
      if (candidate.language === "en") return "en";
      if (candidate.language === "zh" &&
          (candidate.script === "Hant" || (!candidate.script &&
            (!candidate.region || ["TW", "HK", "MO"].includes(candidate.region))))) return "zh-TW";
    } catch {
      // Ignore malformed preferences and try the next browser language.
    }
  }
  return "en";
}

// Resolve once for this page session so async errors and rendered copy agree.
// A reload picks up changed browser language preferences.
export const locale = resolveLocale(typeof navigator === "undefined" ? []
  : navigator.languages?.length ? navigator.languages : [navigator.language]);

export function translate<K extends Message>(language: Locale, message: K, ...args: Arguments<K>): string {
  const template = language === "zh-TW" ? traditionalChinese[message] : message;
  const values = args[0] as Record<string, string | number> | undefined;
  return template.replace(/\{([^{}]+)\}/g, (placeholder, name: string) =>
    values && Object.hasOwn(values, name) ? String(values[name]) : placeholder);
}

export function t<K extends Message>(message: K, ...args: Arguments<K>): string {
  return translate(locale, message, ...args);
}
