CG Paint Studio - C Edition

Build:
  Open an x64 Native Tools Command Prompt for VS 2022, or run build.bat.

Features:
  Pencil, brush, paint bucket, eraser, text, line, rectangle, ellipse, and polygon tools
  Rectangular selection with Delete, color picker, zoom in/out, flip, rotate, resize, and skew
  Color palette, brush size, filled shapes, undo/redo, clear, and PNG export

Controls:
  Select a tool from the Draw or Edit toolbar. Select a canvas region with Select and
  press Delete to remove it. Ctrl+Z and Ctrl+Y undo and redo. Text places a default
  "Text" label using the selected color; the native C implementation keeps the program
  dependency-free.

The application is implemented in C using the Win32 API, GDI, and GDI+.
