// CSS class + data-attribute ad-block filter
// See docs/ad-block-filter.md for full documentation.
//
// Drops elements whose class or data-* attributes match known ad,
// sidebar, modal, cookie-banner, and navigation patterns.

#ifndef ADBLOCK_FILTER_DEFINED
#define ADBLOCK_FILTER_DEFINED

#include "devil-filters.h"
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

/* Case-insensitive prefix check — no regex dependency. */
static inline bool
_ci_starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return false;
        s++;
        prefix++;
    }
    return true;
}

/* Check if a class token matches any ad/noise pattern (case-insensitive). */
static bool
class_token_is_noise(const char *token, size_t len)
{
    char lower[256];
    if (len >= sizeof(lower)) len = sizeof(lower) - 1;
    for (size_t i = 0; i < len; i++)
        lower[i] = tolower((unsigned char)token[i]);
    lower[len] = '\0';

    static const char *ad_pats[] = {
        "advert", "sponsored", "promoted", "native-adv",
        "ad-container", "ad-wrapper", "ad-slot", "ad-space", "ad-area",
        "ad-placeholder", "ad-render", "ad-target", "ad-unit",
        "ad-banner", "ad-display", "ad-frame", "ad-iframe", "ad-img",
        "ad-layer", "ad-message", "ad-overlay", "ad-player", "ad-pod",
        "ad-position", "ad-rendering", "ad-result", "ad-server",
        "ad-slot-", "ad-tag", "ad-text", "ad-track", "ad-view",
        NULL
    };
    for (int i = 0; ad_pats[i]; i++)
        if (strstr(lower, ad_pats[i])) return true;

    static const char *side_pats[] = {
        "sidebar", "side-bar", "aside-nav", "right-col", "left-col",
        "col-", "span-", "grid-col", "aside",
        "widget-area", "widget-container", "widget-box", "widget",
        "related-posts", "related-content", "more-from", "read-more",
        NULL
    };
    for (int i = 0; side_pats[i]; i++)
        if (strstr(lower, side_pats[i])) return true;

    static const char *modal_pats[] = {
        "modal", "overlay", "popup", "dialog", "lightbox", "cta",
        "modal-backdrop", "modal-content", "modal-dialog", "modal-header",
        "modal-body", "modal-footer", "modal-size", "modal-open",
        "popup-", "popup-overlay", "popup-content", "popup-window",
        "lightbox-", "lightbox-overlay", "lightbox-content",
        "tooltip", "float", "fab",
        NULL
    };
    for (int i = 0; modal_pats[i]; i++)
        if (strstr(lower, modal_pats[i])) return true;

    static const char *cookie_pats[] = {
        "cookie", "consent", "gdpr", "banner-consent", "opt-in",
        "cookie-banner", "cookie-notice", "cookie-accept", "cookie-policy",
        "privacy-policy", "privacy-notice",
        NULL
    };
    for (int i = 0; cookie_pats[i]; i++)
        if (strstr(lower, cookie_pats[i])) return true;

    static const char *nav_pats[] = {
        "header-nav", "footer-nav",
        "breadcrumbs", "breadcrumb", "topnav",
        "sidenav", "side-nav", "left-nav", "right-nav",
        NULL
    };
    for (int i = 0; nav_pats[i]; i++)
        if (strstr(lower, nav_pats[i])) return true;

    static const char *footer_pats[] = {
        "footer", "bottom-bar", "page-footer", "site-footer",
        NULL
    };
    for (int i = 0; footer_pats[i]; i++)
        if (strstr(lower, footer_pats[i])) return true;

    static const char *track_pats[] = {
        "analytics", "ga-", "gtag", "piwik", "matomo",
        "mixpanel", "segment", "hotjar", "telemetry",
        "tracking", "tracker", "pixel",
        NULL
    };
    for (int i = 0; track_pats[i]; i++)
        if (strstr(lower, track_pats[i])) return true;

    static const char *social_pats[] = {
        "share-", "social-share", "follow-us", "like-btn", "tweet-btn",
        "fb-like", "fb-share", "twitter-btn", "twitter-share",
        "linkedin-share", "pinterest-btn", "social-button",
        NULL
    };
    for (int i = 0; social_pats[i]; i++)
        if (strstr(lower, social_pats[i])) return true;

    static const char *form_pats[] = {
        "newsletter", "subscribe", "signup", "contact-form", "cta-button",
        "sign-up", "sign_up", "email-subscribe", "mailchimp",
        NULL
    };
    for (int i = 0; form_pats[i]; i++)
        if (strstr(lower, form_pats[i])) return true;

    static const char *video_pats[] = {
        "video-ads", "pre-roll", "ad-player", "ad-container", "ad-wrapper",
        NULL
    };
    for (int i = 0; video_pats[i]; i++)
        if (strstr(lower, video_pats[i])) return true;

    static const char *responsive_pats[] = {
        "visible-xs", "visible-sm", "visible-md", "visible-lg",
        "hidden-xs", "hidden-sm", "hidden-md", "hidden-lg",
        NULL
    };
    for (int i = 0; responsive_pats[i]; i++)
        if (strstr(lower, responsive_pats[i])) return true;

    static const char *junk_pats[] = {
        "sticky", "float-bar", "top-bar", "bottom-bar",
        "page-break", "skip-link", "sr-only", "visually-hidden",
        "skip-navigation", "back-to-top",
        NULL
    };
    for (int i = 0; junk_pats[i]; i++)
        if (strstr(lower, junk_pats[i])) return true;

    return false;
}

/* Check data-* attributes for ad/analytics/privacy markers. */
static bool
data_attr_is_noise(const char *name, size_t name_len,
                   const char *value, size_t value_len)
{
    (void)value;
    (void)value_len;

    static const char *data_pats[] = {
        "data-ad-",
        "data-analytics",
        "data-gtm-",
        "data-track",
        "data-tracking",
        "data-consent",
        "data-cookie",
        "data-privacy",
        "data-geo",
        NULL
    };
    for (int i = 0; data_pats[i]; i++)
        if (_ci_starts_with(name, data_pats[i])) return true;

    return false;
}

/* Check if an element should be pruned (class check + data-* check). */
static bool
adblock_should_prune(lxb_dom_node_t *node, lxb_tag_id_t tag, void *ctx_ptr)
{
    (void)ctx_ptr;

    if (lxb_dom_node_type(node) != LXB_DOM_NODE_TYPE_ELEMENT)
        return false;

    lxb_dom_element_t *elem = (lxb_dom_element_t *)node;

    /* 1. Check class attribute — split on whitespace, check each token */
    lxb_dom_attr_t *class_attr = lxb_dom_element_class_attribute(elem);
    if (class_attr) {
        size_t class_len = 0;
        const lxb_char_t *class_data = lxb_dom_attr_value(class_attr, &class_len);
        if (class_data && class_len > 0) {
            const lxb_char_t *p = (const lxb_char_t *)class_data;
            const lxb_char_t *end = p + class_len;
            while (p < end) {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
                    p++;
                if (p >= end) break;

                const lxb_char_t *token_start = p;
                while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
                    p++;
                size_t token_len = (size_t)(p - token_start);

                if (token_len > 0 && class_token_is_noise((const char *)token_start, token_len))
                    return true;
            }
        }
    }

    /* 2. Check data-* attributes for ad/analytics markers */
    lxb_dom_attr_t *attr = lxb_dom_element_first_attribute(elem);
    while (attr) {
        size_t name_len = 0;
        const lxb_char_t *name_data = lxb_dom_attr_qualified_name(attr, &name_len);
        if (name_data && name_len > 5) {
            bool is_data_attr = false;
            if (name_len >= 5) {
                const lxb_char_t *prefix = (const lxb_char_t *)"data-";
                size_t plen = 5;
                if (name_len >= plen) {
                    is_data_attr = true;
                    for (size_t i = 0; i < plen; i++) {
                        if (((const lxb_char_t *)prefix)[i] != name_data[i]) {
                            is_data_attr = false;
                            break;
                        }
                    }
                }
            }
            if (is_data_attr) {
                if (data_attr_is_noise((const char *)name_data, name_len, NULL, 0))
                    return true;
            }
        }
        attr = lxb_dom_element_next_attribute(attr);
    }

    return false;
}

/* Registration — called from extract_text() when DEVIL_FILTER_AD_BLOCK is defined. */
void
adblock_register(walk_ctx_t *ctx)
{
    ctx->pre_filter = adblock_should_prune;
}

#endif /* ADBLOCK_FILTER_DEFINED */
