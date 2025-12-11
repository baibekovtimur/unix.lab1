#!/bin/sh

# Коды выхода
# 0 - Успех
# 1 - Отсутствие входного файла в аргументах
# 2 - Не найден исходный файл
# 3 - Ошибка создания временного каталога
# 4 - Не найдено имя выходного файла
# 5 - Компилятор не найден
# 6 - Неподдерживаемый тип файла
# 7 - Ошибка перехода в временный каталог
# 8 - Не найден выходной файл после сборки
# 9 - Ошибка сборки

# Функция для очистки временных файлов
cleanup() {
    if [ -n "$tmp_dir" ] && [ -d "$tmp_dir" ]; then
        rm -rf "$tmp_dir"
    fi
}

# Установка обработчиков сигналов для гарантированной очистки
trap cleanup EXIT INT TERM HUP

# Проверка наличия исходного файла в аргументах
if [ $# -ne 1 ]; then
    echo "Usage: $0 <source_file>" >&2
    exit 1
fi

source_file="$1"
source_dir=$(dirname "$source_file")
source_base=$(basename "$source_file")

# Проверка существования исходного файла
if [ ! -f "$source_file" ]; then
    echo "Error: Source file '$source_file' not found" >&2
    exit 2
fi

# Сохраняем абсолютный путь к исходному каталогу
original_dir=$(pwd)

# Создание временного каталога
tmp_dir=$(mktemp -d)
if [ $? -ne 0 ]; then
    echo "Error: Failed to create temporary directory" >&2
    exit 3
fi

echo "Using temporary directory: $tmp_dir"

# Поиск имени выходного файла в комментариях
output_name=$(grep -E '^[[:space:]]*//[[:space:]]*Output:[[:space:]]*' "$source_file" | \
              head -1 | \
              sed 's/^[[:space:]]*\/\/[[:space:]]*Output:[[:space:]]*//')

# Если не нашли в C++ стиле, пробуем C стиль
if [ -z "$output_name" ]; then
    output_name=$(grep -E '^[[:space:]]*/\*[[:space:]]*Output:[[:space:]]*' "$source_file" | \
                  head -1 | \
                  sed 's/^[[:space:]]*\/\*[[:space:]]*Output:[[:space:]]*//' | \
                  sed 's/[[:space:]]*\*\/.*$//')
fi

# Если не нашли в C/C++ стиле, пробуем TeX стиль
if [ -z "$output_name" ]; then
    output_name=$(grep -E '^[[:space:]]*%[[:space:]]*Output:[[:space:]]*' "$source_file" | \
                  head -1 | \
                  sed 's/^[[:space:]]*%[[:space:]]*Output:[[:space:]]*//')
fi

# Проверка, что имя выходного файла найдено
if [ -z "$output_name" ]; then
    echo "Error: Could not find Output directive in '$source_file'" >&2
    exit 4
fi

# Удаляем возможные кавычки и пробелы вокруг имени
output_name=$(echo "$output_name" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//; s/^"//; s/"$//; s/^'"'"'//; s/'"'"'$//')

echo "Found output name: '$output_name'"

# Определяем тип файла и команду для сборки
file_ext=$(echo "$source_base" | sed 's/.*\.//')
case "$file_ext" in
    c)
        build_cmd="cc -o \"$output_name\" \"$source_base\""
        ;;
    cpp|cxx|cc)
        # Пробуем C++ компиляторы в порядке предпочтения
        for compiler in c++ g++ clang++; do
            if command -v "$compiler" >/dev/null 2>&1; then
                build_cmd="$compiler -o \"$output_name\" \"$source_base\""
                echo "Using compiler: $compiler"
                break
            fi
        done
        if [ -z "$build_cmd" ]; then
            echo "Error: No C++ compiler found" >&2
            exit 5
        fi
        ;;
    tex)
        # Пробуем LaTeX компиляторы
        base_name="${output_name%.*}"
        if [ -z "$base_name" ] || [ "$base_name" = "$output_name" ]; then
            base_name="$output_name"
        fi
        for compiler in pdflatex lualatex xelatex; do
            if command -v "$compiler" >/dev/null 2>&1; then
                build_cmd="$compiler -interaction=nonstopmode -jobname=\"$base_name\" \"$source_base\""
                echo "Using compiler: $compiler"
                break
            fi
        done
        if [ -z "$build_cmd" ]; then
            echo "Error: No LaTeX compiler found" >&2
            exit 5
        fi
        ;;
    *)
        echo "Error: Unsupported file type '.$file_ext'" >&2
        exit 6
        ;;
esac

# Переходим во временный каталог и выполняем сборку
cd "$tmp_dir" || {
    echo "Error: Cannot enter temporary directory" >&2
    exit 7
}

# Копируем исходный файл во временный каталог
cp "$original_dir/$source_file" "./$source_base"

echo "Building $source_base -> $output_name"
echo "Command: $build_cmd"

# Выполняем сборку
if eval "$build_cmd"; then
    # Если сборка успешна, копируем результат обратно
    if [ -f "$output_name" ]; then
        cp "$output_name" "$original_dir/"
        echo "Build successful: $output_name"
    else
        # Для LaTeX определяем ожидаемый выходной файл
        if [ "$file_ext" = "tex" ]; then
            expected_pdf="${output_name%.*}.pdf"
            if [ -f "$expected_pdf" ]; then
                cp "$expected_pdf" "$original_dir/"
                echo "Build successful: $expected_pdf"
            else
                # Пробуем найти любой PDF файл
                pdf_file=$(find . -name "*.pdf" -type f | head -1)
                if [ -n "$pdf_file" ]; then
                    cp "$pdf_file" "$original_dir/"
                    echo "Build successful: $(basename "$pdf_file")"
                else
                    echo "Error: No output PDF file found" >&2
                    exit 8
                fi
            fi
        else
            echo "Error: Output file '$output_name' not found after build" >&2
            exit 8
        fi
    fi
else
    echo "Error: Build failed" >&2
    exit 9
fi

# Выход с успешным кодом
exit 0
