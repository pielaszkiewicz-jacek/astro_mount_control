/** @type {import('tailwindcss').Config} */
module.exports = {
  content: [
    '../public/index.html',
    '../public/js/**/*.js',
  ],
  darkMode: 'class',
  theme: {
    extend: {
      fontFamily: {
        sans: [
          'Inter',
          'ui-sans-serif',
          'system-ui',
          '-apple-system',
          'BlinkMacSystemFont',
          'Segoe UI',
          'Roboto',
          'sans-serif',
        ],
      },
    },
  },
  plugins: [require('flowbite/plugin')],
};
