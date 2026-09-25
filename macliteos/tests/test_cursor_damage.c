#include "ml/common.h"
#include "ml/region.h"
#include <stdio.h>

static ml_rect cursor_damage(int x, int y)
{
    return ml_rect_make(x - 4, y - 4, 24, 28);
}

static int contains(ml_rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

int main(void)
{
    ml_region damage;
    ml_region_init(&damage);
    int positions[][2] = {
        {12, 14}, {40, 36}, {78, 62}, {120, 88},
        {164, 111}, {210, 136}, {260, 165}, {300, 190}
    };

    for (size_t i = 1; i < ML_ARRAY_SIZE(positions); i++) {
        ml_region_clear(&damage);
        ml_rect oldr = cursor_damage(positions[i - 1][0], positions[i - 1][1]);
        ml_rect newr = cursor_damage(positions[i][0], positions[i][1]);
        ml_region_add(&damage, oldr);
        ml_region_add(&damage, newr);

        if (!contains(oldr, positions[i - 1][0], positions[i - 1][1]) ||
            !contains(newr, positions[i][0], positions[i][1])) {
            fprintf(stderr, "FAIL: cursor endpoint is outside its damage rectangle\n");
            return 1;
        }
        if (ml_region_empty(&damage) || ml_region_area(&damage) <= 0) {
            fprintf(stderr, "FAIL: cursor movement generated no damage\n");
            return 1;
        }
    }

    /* Old + new endpoints must both be represented; otherwise old cursor
     * pixels can survive in a damage-only framebuffer. */
    ml_region_clear(&damage);
    ml_region_add(&damage, cursor_damage(40, 40));
    ml_region_add(&damage, cursor_damage(260, 160));
    ml_rect bounds = ml_region_bounds(&damage);
    if (bounds.w < 200 || bounds.h < 100) {
        fprintf(stderr, "FAIL: old/new cursor damage was collapsed incorrectly\n");
        return 1;
    }

    ml_region_free(&damage);
    printf("PASS: cursor old/new damage and rapid/diagonal movement coverage\n");
    return 0;
}
