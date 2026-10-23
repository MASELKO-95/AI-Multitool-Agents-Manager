#ifndef AIMT_NUKLEAR_TOOLTIP_H
#define AIMT_NUKLEAR_TOOLTIP_H

/* Include after nuklear.h. A tooltip cannot share an active popup slot or
 * appear inside a popup (including its groups). In this Nuklear version a
 * failed tooltip begin can also change the caller's panel type. */
static void aimt_ui_tooltip(struct nk_context *ctx, const char *text)
{
    if (!ctx || !ctx->current || !ctx->current->layout || !text) return;
    if (ctx->current->popup.active) return;
    for (const struct nk_panel *panel = ctx->current->layout; panel; panel = panel->parent)
        if ((int)panel->type & (int)NK_PANEL_SET_POPUP) return;
    nk_tooltip(ctx, text);
}

#endif
