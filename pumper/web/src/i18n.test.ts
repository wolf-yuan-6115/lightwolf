import { describe, expect, it } from "vitest";
import { resolveLocale, translate } from "./i18n";
import traditionalChinese from "./locales/zh-TW.json";

describe("browser language matching", () => {
  it.each(["zh", "zh-TW", "zh-HK", "zh-MO", "zh-Hant", "zh-Hant-HK", "zh-Hant-US", "ZH-tw"]) (
    "uses Traditional Chinese for %s", (language) => {
      expect(resolveLocale([language])).toBe("zh-TW");
    },
  );

  it.each(["en", "en-US", "en-GB", "ja-JP", "zh-CN", "zh-Hans", "zh-Hans-TW"]) (
    "uses English for %s", (language) => {
      expect(resolveLocale([language])).toBe("en");
    },
  );

  it("respects preference order and skips unsupported or malformed languages", () => {
    expect(resolveLocale(["en-US", "zh-TW"])).toBe("en");
    expect(resolveLocale(["zh-TW", "en-US"])).toBe("zh-TW");
    expect(resolveLocale(["ja-JP", "invalid_locale", "zh-Hant"])).toBe("zh-TW");
    expect(resolveLocale(["zh-Hans", "en-GB", "zh-TW"])).toBe("en");
    expect(resolveLocale([])).toBe("en");
    expect(resolveLocale([""])).toBe("en");
  });
});

describe("message interpolation", () => {
  it("preserves English copy and uses complete translated sentences", () => {
    expect(translate("en", "Save Profile {number}?", { number: 2 })).toBe("Save Profile 2?");
    expect(translate("zh-TW", "Save Profile {number}?", { number: 2 })).toBe("要儲存設定檔 2 嗎？");
    expect(translate("zh-TW", "{label} must be between {min} and {max}{unit}.", {
      label: "頻率", min: "20", max: "20,000", unit: " Hz",
    })).toBe("「頻率」必須介於 20 與 20,000 Hz 之間。");
  });

  it("inserts error details literally without recursively interpolating them", () => {
    expect(translate("zh-TW", "Audio settings: {error}", { error: "{error} $& <test>" }))
      .toBe("音訊設定：{error} $& <test>");
  });

  it("has nonempty translations with matching placeholders throughout the catalog", () => {
    const placeholders = (text: string) => [...text.matchAll(/\{([^{}]+)\}/g)].map((match) => match[1]).sort();
    for (const [source, translation] of Object.entries(traditionalChinese)) {
      expect(translation.trim(), source).not.toBe("");
      expect(placeholders(translation), source).toEqual(placeholders(source));
    }
  });
});
