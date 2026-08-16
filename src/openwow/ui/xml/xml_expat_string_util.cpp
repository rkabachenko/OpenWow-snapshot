
#include "xml_expat_string_util.h"

namespace openwow::ui::xml {

int streqci(const char *a, const char *b) {
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);

        if (static_cast<unsigned char>(cb - 'a') <= 25u)
            cb -= 32;
        if (static_cast<unsigned char>(ca - 'a') <= 25u)
            ca -= 32;

        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;

        ++a;
        ++b;
    }
}

char *normalizePublicId(char *publicId) {
    char *write = publicId;
    const char *read = publicId;

    if (!*read) {
        *write = '\0';
        return write;
    }

    do {
        const char c = *read;

        if (c == '\n' || c == '\r' || c == ' ') {

            if (write != publicId && *(write - 1) != ' ') {
                *write++ = ' ';
            }
        } else {
            *write++ = c;
        }

        ++read;
    } while (*read);

    if (write != publicId && *(write - 1) == ' ') {
        *(write - 1) = '\0';
        return write;
    }

    *write = '\0';
    return write;
}

char *normalizeLines(char *str) {
    char *read = str;
    char c = *read;

    if (!c)
        return read;

    while (c != '\r') {
        c = *++read;
        if (!c)
            return read;
    }

    char *write = read;

    while (*read == '\r') {
        *write = '\n';
        ++read;
        ++write;

        if (*read == '\n')
            ++read;

        if (!*read) {
            *write = '\0';
            return read;
        }
    }

    while (*read) {
        if (*read == '\r') {
            *write++ = '\n';
            ++read;
            if (*read == '\n')
                ++read;
        } else {
            *write++ = *read++;
        }
    }

    *write = '\0';
    return read;
}

uint32_t hashString(const uint8_t *str) {
    uint32_t hash = 0;
    uint8_t c = *str;

    while (c) {
        hash = hash * 33 + c;
        c = *++str;
    }

    return hash;
}

}
