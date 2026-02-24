/** @type {import('tailwindcss').Config} */
module.exports = {
  content: [
    "./app/**/*.{js,jsx,ts,tsx}",
    "./components/**/*.{js,jsx,ts,tsx}",
    "./src/**/*.{js,jsx,ts,tsx}",
  ],
  presets: [require("nativewind/preset")],
  theme: {
    extend: {
      colors: {
        // Core app palette (matches your screenshot style)
        star: {
          bg: "#2F344B", // main background (deep navy)
          surface: "#3A405A", // optional card/panel surface
          text: "#E9EAF2", // title/primary text
          muted: "#B8BCCB", // secondary text
          button: "#ECEDEF", // button fill (off-white)
          buttonText: "#2F344B", // text on button
          border: "rgba(236,237,239,0.25)",
          accent: "#6D7CFF", // optional accent (focus/active)
          accent2: "#A78BFA", // optional secondary accent
          danger: "#EF4444",
          success: "#22C55E",
        },
      },

      // Nice rounded pill buttons like your mock
      borderRadius: {
        pill: "9999px",
      },
    },
  },
  plugins: [],
};
