#include <curses.h>
#if defined(WIN32) || defined(__DOS__)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define VERSION_MAJOR 0
#define VERSION_MINOR 9
#define VERSION_REVISION 0

#define STR_(x) #x
#define STR(x) STR_(x)

#if !defined(min)
#define min(a, b) \
    (((a) < (b)) ? (a) : (b))
#endif

#if !defined(max)
#define max(a, b) \
    (((a) > (b)) ? (a) : (b))
#endif

#define KEY_ESC 27
#define KEY_CTRL(x) ((x) > 60 ? (x)-0x60 : (x)-0x40)

#if defined(__DOS__)
typedef unsigned long offset_t;
#else
typedef size_t offset_t;
#endif

enum edit_mode { HEX, ASCII };

struct buffer
{
    FILE* file;
    offset_t filesize;
    offset_t offset;
    size_t size;
    int valid;
    unsigned char* buffer;
};

void buffer_destroy(struct buffer* b)
{
    free(b->buffer);
    b->file = NULL;
    b->offset = 0;
    b->size = 0;
    b->valid = 0;
    b->buffer = NULL;
}

int buffer_create(struct buffer* b, size_t size, FILE* file)
{
    b->file = file;
    if (fseek(file, 0, SEEK_END) != 0)
    {
        goto error;
    }
    b->filesize = ftell(file);
    if (fseek(file, 0, SEEK_SET) != 0)
    {
        goto error;
    }
    b->offset = 0;
    b->buffer = malloc(size);
    if (b->buffer != NULL)
    {
        b->size = size;
        if (b->filesize == 0
            || fread(b->buffer, min(b->size, b->filesize), 1, b->file) == 1)
        {
            return 1;
        }
    }
error:
    buffer_destroy(b);
    return 0;
}

unsigned char* buffer_access(struct buffer* b, offset_t offset, size_t size)
{
    if (size > b->size)
    {
        return NULL;
    }
    if (offset >= b->filesize)
    {
        return NULL;
    }
    if (!b->valid || offset < b->offset || offset + size > b->offset + b->size)
    {
        if (!b->valid)
        {
            b->offset = offset;
        }
        else if (offset < b->offset)
        {
            if (offset + size > b->size)
            {
                b->offset = offset + size - b->size;
            }
            else
            {
                b->offset = 0;
            }
        }
        else // offset+size > b->offset+b->size
        {
            if (offset + b->size > b->filesize)
            {
                b->offset = b->filesize - b->size;
            }
            else
            {
                b->offset = offset;
            }
        }
        fseek(b->file, b->offset, SEEK_SET);
        if (fread(b->buffer,
                  min(b->size, b->filesize - b->offset),
                  1,
                  b->file) != 1)
        {
            return NULL;
        }
        b->valid = 1;
    }
    return &b->buffer[offset - b->offset];
}

enum buffer_search_direction
{
    BUFFER_FORWARD,
    BUFFER_BACKWARD
};

int buffer_search(struct buffer* b, offset_t offset, size_t search_length,
        unsigned char* search_target, enum buffer_search_direction d,
        offset_t* match_offset)
{
    size_t match_block_size = min(search_length, b->size);
    offset_t fo;
    offset_t mo;

    if (search_length == 0)
    {
        return 0;
    }
    for (
        fo = offset;
        fo < b->filesize - search_length;
        (d == BUFFER_FORWARD) ? fo++ : fo--)
    {
        mo = 0;
        for (mo = 0; mo < search_length; mo += match_block_size)
        {
            unsigned char* page = buffer_access(b, fo, match_block_size);
            if (memcmp(page,
                       &search_target[mo],
                       min(search_length - mo, match_block_size)) != 0)
            {
                break;
            }
        }
        if (mo >= search_length)
        {
            *match_offset = fo;
            return 1;
        }
        if (d == BUFFER_BACKWARD && fo == 0)
        {
            break;
        }
    }
    return 0;
}

void buffer_invalidate(struct buffer* b)
{
    b->valid = 0;
}

int buffer_write(struct buffer* b, size_t offset, size_t size,
        unsigned char* data)
{
    if (fseek(b->file, offset, SEEK_SET) != 0)
    {
        return 0;
    }
    // Not updating b->offset here, because the data in b->buffer is
    // still from the original b->offset.
    if (fwrite(data, size, 1, b->file) != 1)
    {
        return 0;
    }
    // Re-read b->buffer if changed on disk:
    if ((offset < b->offset + b->size) && (offset + size >= b->offset))
    {
        if (fseek(b->file, b->offset, SEEK_SET) != 0)
        {
            return 0;
        }
        if (fread(b->buffer,
                  min(b->size, b->filesize - b->offset),
                  1,
                  b->file) != 1)
        {
            return 0;
        }
    }
    return 1;
}

int buffer_insert(struct buffer* b, offset_t offset, size_t size)
{
    unsigned char* buffer = NULL;
    offset_t tailsize;
    offset_t o;

    offset = min(offset, b->filesize);
    size = min(size, ~(size_t)0 - b->filesize);
    tailsize = b->filesize - offset;

    // Append to file:
    buffer_invalidate(b);
    memset(b->buffer, 0, b->size);
    b->filesize += size;
    for (o = 0; o < size; o += b->size)
    {
        size_t chunksize = min(size - o, b->size);
        if (buffer_write(b, b->filesize + o - size, chunksize, b->buffer) == 0)
        {
            return 0;
        }
    }
    // Move trailing part towards back:
    for (o = 0; o < tailsize; o += b->size)
    {
        size_t chunksize = min(tailsize - o, b->size);
        if ((buffer = buffer_access(b,
                                    b->filesize - (size + o + chunksize),
                                    chunksize)) == NULL)
        {
            return 0;
        }
        if (buffer_write(b,
                         b->filesize - (o + chunksize),
                         chunksize,
                         b->buffer) == 0)
        {
            return 0;
        }
    }
    // Initialize the inserted bytes:
    for (o = 0; o < size; o += b->size)
    {
        size_t chunksize = min(size - o, b->size);
        buffer_invalidate(b);
        memset(b->buffer, 0, chunksize);
        if (buffer_write(b, offset + o, chunksize, b->buffer) == 0)
        {
            return 0;
        }
    }
    return 1;
}

int buffer_remove(struct buffer* b, offset_t offset, size_t size)
{
    offset_t o;

    offset = min(offset, b->filesize);
    size = min(size, b->filesize - offset);
    for (o = offset + size; o < b->filesize; o += b->size)
    {
        size_t chunksize = min(b->size, b->filesize - o);
        unsigned char* buffer = NULL;
        if ((buffer = buffer_access(b, o, chunksize)) == NULL)
        {
            return 0;
        }
        if (buffer_write(b, o - size, chunksize, buffer) == 0)
        {
            return 0;
        }
    }
    b->filesize -= size;
#if defined(__DOS__)
    (void)!chsize(fileno(b->file), b->filesize);
#elif defined(WIN32)
    (void)!_chsize(_fileno(b->file), b->filesize);
#else
    (void)!ftruncate(fileno(b->file), b->filesize);
#endif
    return 1;
}

static int is_hex(int c)
{
    return ((c >= '0') && (c <= '9'))
        || ((c >= 'A') && (c <= 'F'))
        || ((c >= 'a') && (c <= 'f'));
}

static int is_printable_ascii(int c)
{
    return (c >= ' ') && (c <= '~');
}

static int hex_char_to_nibble(int c)
{
    if (is_hex(c))
    {
        int nibble = c - '0';
        if (nibble > 9)
        {
            nibble -= 'A' - '9' - 1;
        }
        if (nibble > 15)
        {
            nibble -= 'a' - 'A';
        }
        return nibble;
    }
    return 0;
}

static int ascii_x_pos(int offset)
{
    return offset % 16;
}

static int ascii_y_pos(int offset)
{
    return offset / 16;
}

static int hex_x_pos(int offset)
{
    int rem = offset % 16;
    return 3*rem + rem/8;
}

static int hex_y_pos(int offset)
{
    return offset / 16;
}

static void display_contents(offset_t size, offset_t offset,
        unsigned char* page, int lines)
{
    offset_t o = 0;
    int y;
    int i;

    for (y = 0; y < lines; y++)
    {
        if (offset + o < size)
        {
#if defined(__DOS__)
            mvprintw(y, 0, "%08lX", offset + o);
#else
            mvprintw(y, 0, "%08zX", offset + o);
#endif

            for (i = 0; i < 16; i++)
            {
                if (offset + o < size)
                {
                    unsigned char byte = page[o];

                    mvprintw(hex_y_pos(o), 10 + hex_x_pos(o), "%02X", byte);
                    mvaddch(ascii_y_pos(o), 61 + ascii_x_pos(o),
                        isprint(byte) ? byte : '.');
                    ++o;
                }
            }

            mvaddch(y, 60, '|');
            mvaddch(y, 62 + ((o - 1) % 16), '|');
        }
        else
        {
#if defined(__DOS__)
            mvprintw(y, 0, "%08lX", size);
#else
            mvprintw(y, 0, "%08zX", size);
#endif
            break;
        }
    }
}

static void set_cursor(enum edit_mode edit_mode, offset_t offset,
        offset_t cursor)
{
    switch (edit_mode)
    {
        case HEX:
            move(0 + hex_y_pos(cursor / 2 - offset),
                 10 + hex_x_pos(cursor / 2 - offset) + cursor % 2);
            break;

        case ASCII:
            move(0 + ascii_y_pos(cursor / 2 - offset),
                 61 + ascii_x_pos(cursor / 2 - offset));
            break;
    }
}

static int get_number(const char* prompt, offset_t* number, int hex)
{
    char numstr[21] = { 0 };
    unsigned int pos = 0;
    int y;
    int key;

    WINDOW* win = newwin(3, COLS, (LINES - 3) / 2, 0);
    wattron(win, A_REVERSE);
    for (y = 0; y < 3; y++)
    {
        mvwhline(win, y, 0, ' ', COLS);
    }
    mvwaddstr(win, 1, 1, prompt);
    mvwaddstr(win, 1, 1 + strlen(prompt) + 1, hex ? "(hex)" : "(dec)");
    wmove(win, 1, 1 + strlen(prompt) + 7);

    wrefresh(win);
    keypad(win, TRUE);
    for (key = 0; (key != KEY_ESC) && (key != KEY_ENTER);)
    {
        switch (key = wgetch(win))
        {
            case KEY_ESC: // TERMINATE
            case 'q':
            case KEY_CTRL('c'):
                key = KEY_ESC;
                delwin(win);
                return 0;

            case KEY_RESIZE:
                resize_term(0, 0);
                break;

            case 9:
            case 'X':
            case 'x':
            case KEY_LEFT:
            case KEY_RIGHT:
            case KEY_UP:
            case KEY_DOWN:
                hex = !hex;
                mvwaddstr(win, 1, 1 + strlen(prompt) + 1,
                          hex ? "(hex)" : "(dec)");
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
                if (pos < sizeof(numstr) - 1)
                {
                    numstr[pos++] = toupper(key);
                }
                numstr[pos] = '\0';
                break;

            case KEY_BACKSPACE:
            case 8:
                if (pos > 0)
                {
                    numstr[--pos] = '\0';
                }
                break;

            case KEY_ENTER:
            case 10:
            case 13:
                *number = strtoul(numstr, NULL, hex ? 16 : 10);
                delwin(win);
                return 1;
        }
        mvwhline(win, 1, 1 + strlen(prompt) + 7, ' ', sizeof(numstr) - 1);
        mvwaddstr(win, 1, 1 + strlen(prompt) + 7, numstr);
        wrefresh(win);
    }
    delwin(win);
    return 0;
}

static int get_data(const char* prompt, int max_length, unsigned char* target,
        int* target_length, int hex)
{
    int pos = 0;
    int y;
    int key;

    WINDOW* win = newwin(3, COLS, (LINES - 3) / 2, 0);
    wattron(win, A_REVERSE);
    for (y = 0; y < 3; y++)
    {
        mvwhline(win, y, 0, ' ', COLS);
    }
    mvwaddstr(win, 1, 1, prompt);
    mvwaddstr(win, 1, 1 + strlen(prompt) + 1, hex ? "(hex)  " : "(ASCII)");
    wmove(win, 1, 1 + strlen(prompt) + 9);

    wrefresh(win);
    keypad(win, TRUE);
    for (key = 0; (key != KEY_ESC) && (key != KEY_ENTER);)
    {
        switch (key = wgetch(win))
        {
            case KEY_ESC: // TERMINATE
            case 'q':
            case KEY_CTRL('c'):
                key = KEY_ESC;
                delwin(win);
                return 0;

            case KEY_RESIZE:
                resize_term(0, 0);
                break;

            case 9:
            case KEY_LEFT:
            case KEY_RIGHT:
            case KEY_UP:
            case KEY_DOWN:
                hex = !hex;
                mvwaddstr(win, 1, 1 + strlen(prompt) + 1,
                          hex ? "(hex)  " : "(ASCII)");
                break;

            case KEY_BACKSPACE:
            case 8:
                if (pos > 0) target[--pos] = '\0';
                break;

            case KEY_ENTER:
            case 10:
            case 13:
                delwin(win);
                *target_length = (pos+1)/2;
                return 1;

            default:
                if (max_length > 0 && pos / 2 < max_length - 1)
                {
                  if (hex)
                  {
                    if (is_hex(key))
                    {
                      if (pos % 2 == 0)
                      {
                        target[pos / 2] = hex_char_to_nibble(key) << 4;
                      }
                      else
                      {
                        target[pos / 2] |= hex_char_to_nibble(key);
                      }
                      ++pos;
                    }
                  }
                  else
                  {
                    if (is_printable_ascii(key))
                    {
                      target[pos / 2] = key;
                      pos += 2;
                      pos &= ~1;
                    }
                  }
                  target[pos] = '\0';
                }
                break;
        }
        mvwhline(win, 1, 1 + strlen(prompt) + 9, ' ', 2 * max_length - 1);
        if (hex)
        {
            int i;
            for (i = 0; i < (pos + 1)/2; i++)
            {
              mvwprintw(win, 1, 1 + strlen(prompt) + 9 + 2 * i, "%02X",
                        target[i]);
            }
            wmove(win, 1, 1 + strlen(prompt) + 9 + pos);
        }
        else
        {
          mvwaddstr(win, 1, 1 + strlen(prompt) + 9, (char*)target);
        }
        wrefresh(win);
    }
    delwin(win);
    return 0;
}

static void handle_keyboard(int* key, struct buffer* b, offset_t* offset,
        offset_t* cursor, enum edit_mode* edit_mode)
{
    static unsigned char search_buffer[64];
    static int search_len = -1;
    *key = getch();

    switch (*key)
    {
        case 0: // IGNORE
            break;

        case KEY_ESC: // TERMINATE
        case KEY_CTRL('c'):
            *key = KEY_ESC;
            break;

        case KEY_RESIZE: // TERMINAL RESIZED
            resize_term(0, 0);
            break;

        case '\t': // SWITCH EDIT MODE
            *edit_mode = *edit_mode == HEX ? ASCII : HEX;
            break;

        case KEY_CTRL('g'): // GO TO OFFSET
        case KEY_CTRL('o'):
            {
                offset_t gotooffset;
                if (get_number("Go to offset:", &gotooffset, 1))
                {
                    *cursor = 2 * min(b->filesize, gotooffset);
                }
            }
            break;

        case KEY_HOME: // GO TO START
            *cursor = 0;
            break;

        case KEY_PPAGE: // GO PAGE UP
            if (*offset >= 16U * LINES)
            {
                *offset -= 16 * LINES;
                *cursor = (*cursor - 2 * 16 * LINES) & ~1;
            }
            else
            {
                *offset = 0;
                *cursor = (*cursor % 16 * LINES) & ~1;
            }
            break;

        case KEY_UP: // GO LINE UP
            if (*cursor / 2 >= 16)
            {
                *cursor = (*cursor - 2 * 16) & ~1;
            }
            break;

        case KEY_LEFT: // GO BYTE UP
            if (*cursor / 2 >= 1)
            {
                *cursor = (*cursor - 2) & ~1;
            }
            break;

        case KEY_RIGHT: // GO BYTE DOWN
            if (*cursor / 2 + 1 < b->filesize + 1)
            {
                *cursor = (*cursor + 2) & ~1;
            }
            break;

        case KEY_DOWN: // GO LINE DOWN
            if (*cursor / 2 + 16 < b->filesize + 1)
            {
                *cursor = (*cursor + 2 * 16) & ~1;
            }
            break;

        case KEY_NPAGE: // GO PAGE DOWN
            if (*offset + 16 * LINES < b->filesize)
            {
                *offset += 16 * LINES;
                if (*cursor / 2 + 16 * LINES < b->filesize + 1)
                {
                    *cursor = (*cursor + 2 * 16 * LINES) & ~1;
                }
                else
                {
                    *cursor = 2 * b->filesize - 1;
                }
            }
            break;

        case KEY_END: // GO TO END
            *offset = ((b->filesize - 1) / (16U * LINES)) * (16U * LINES);
            *cursor = 2 * b->filesize;
            break;

        case KEY_CTRL('f'): // FIND
        case KEY_CTRL('s'): // SEARCH
            search_buffer[0] = '\0';
            if (get_data("Find data:", sizeof(search_buffer), search_buffer,
                         &search_len, *edit_mode == HEX))
            {
                offset_t match_offset = 0;
                if (buffer_search(b, *cursor/2, search_len, search_buffer,
                                  BUFFER_FORWARD, &match_offset))
                {
                    *cursor = 2 * match_offset;
                }
            }
            break;

        case KEY_CTRL('n'): // NEXT FIND/SEARCH MATCH
            if (search_len > 0)
            {
                offset_t match_offset = 0;
                if (buffer_search(b, *cursor/2+1, search_len, search_buffer,
                                  BUFFER_FORWARD, &match_offset))
                {
                    *cursor = 2 * match_offset;
                }
            }
            break;

        case KEY_CTRL('p'): // PREVIOUS FIND/SEARCH MATCH
            if (*cursor/2 > 0 && search_len > 0)
            {
                offset_t match_offset = 0;
                if (buffer_search(b, *cursor/2-1, search_len, search_buffer,
                                  BUFFER_BACKWARD, &match_offset))
                {
                    *cursor = 2 * match_offset;
                }
            }
            break;

        case KEY_IC: // INSERT
            {
                offset_t insertcount;
                if (get_number("Number of bytes to insert:", &insertcount, 0))
                {
                    buffer_insert(b, *cursor / 2, insertcount);
                }
            }
            break;

        case KEY_DC: // REMOVE
            {
                offset_t removecount;
                if (get_number("Number of bytes to remove:", &removecount, 0))
                {
                    buffer_remove(b, *cursor / 2, removecount);
                }
            }
            break;

        case KEY_BACKSPACE:
        case 8: // BACKSPACE
            {
                offset_t removecount;
                if (get_number("Number of bytes to remove:", &removecount, 0))
                {
                    buffer_remove(b, *cursor / 2 - removecount, removecount);
                    if (*cursor / 2 > removecount)
                    {
                        *cursor = (*cursor - 2 * removecount) & ~1;
                    }
                    else
                    {
                        *cursor = 0;
                    }
                }
            }
            break;
    }

    switch (*edit_mode)
    {
        case HEX:
            if (is_hex(*key))
            {
                int byte;
                int nibble;

                if (*cursor / 2 >= b->filesize)
                {
                    buffer_insert(b, b->filesize, 1);
                }
                byte = *buffer_access(b, *cursor / 2, 1);
                nibble = hex_char_to_nibble(*key);
                if (*cursor % 2 == 0)
                {
                    byte = (byte & 0x0F) | (nibble << 4);
                }
                else
                {
                    byte = (byte & 0xF0) | nibble;
                }
                buffer_write(b, *cursor / 2, 1, (unsigned char*)&byte);
                ++*cursor;
            }
            break;

        case ASCII:
            if (is_printable_ascii(*key))
            {
                if (*cursor / 2 >= b->filesize)
                {
                    buffer_insert(b, b->filesize, 1);
                }
                buffer_write(b, *cursor / 2, 1, (unsigned char*)key);
                *cursor += 2;
            }
            break;
    }
    while (*cursor/2 < *offset) *offset -= 16;
    while (*cursor/2 > *offset + 16*(LINES-0) - 1) *offset += 16;
}

static void ui_loop(const char* srcname, struct buffer* b)
{
    offset_t offset = 0;
    offset_t cursor = 0;
    enum edit_mode edit_mode = HEX;
    int key;

    (void)srcname;
    for (key = 0; key != KEY_ESC;)
    {
        unsigned char* page = NULL;

        page = buffer_access(b, offset, 16U*LINES);

        clear();
        display_contents(b->filesize, offset, page, LINES);
        set_cursor(edit_mode, offset, cursor);
        wnoutrefresh(stdscr);
        {
            doupdate();
            handle_keyboard(&key, b, &offset, &cursor, &edit_mode);
        }
    }
}

int main(int argc, char* argv[])
{
    int retval = 0;
    const char* name = NULL;
    FILE* file = NULL;
    size_t buffersize = 4*1024;
    struct buffer b = {0};

    puts("Simple and portable hex editor."
         " Version " STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "."
         STR(VERSION_REVISION) ".\n");

    if (argc != 2)
    {
        fprintf(stderr,
          "Usage:\n"
          "    %s <filename>\n",
          argv[0]);
        retval = -1;
        goto cleanup;
    }

    name = argv[1];
    file = fopen(name, "r+b");
    if (file == NULL)
    {
        fprintf(stderr, "Cannot open file: %s\n", name);
        retval = -1;
        goto cleanup;
    }
    if (!buffer_create(&b, buffersize, file))
    {
        fputs("Could not create buffer.\n", stderr);
        retval = -2;
        goto cleanup;
    }

    initscr();
    cbreak();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(2);

    ui_loop(name, &b);

    move(0, 0);
    clear();
    refresh();
    endwin();
cleanup:
    buffer_destroy(&b);
    if (file != NULL)
    {
        fclose(file);
    }
    return retval;
}
