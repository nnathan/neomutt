#define TEST_NO_MAIN
#define MAIN_C 1
#include "acutest.h"

#include <assert.h>
#include <string.h>
#include "mutt/buffer.h"
#include "mutt/memory.h"
#include "pattern.h"

#include "alias.h"
#include "globals.h"
bool ResumeEditedDraftFiles;

/* All tests are limited to patterns that are stringmatch type only,
 * such as =s, =b, =f, etc.
 *
 * Rationale: (1) there is no way to compare regex types as "equal",
 *            (2) comparing Group is a pain in the arse,
 *            (3) similarly comparing lists (ListHead) is annoying.
 */

/* canoncial representation of Pattern "tree",
 * s specifies a caller allocated buffer to write the string,
 * pat specifies the pattern,
 * indent specifies the indentation level (set to 0 if pat is root of tree),
 * returns the number of characters written to s (not including trailing '\0')
 *
 * A pattern tree with patterns a, b, c, d, e, f, g can be represented graphically
 * as follows (where a is obviously the root):
 *
 *        +-c-+
 *        |   |
 *    +-b-+   +-d
 *    |   |
 *  a-+   +-e
 *    |
 *    +-f-+
 *        |
 *        +-g
 *
 *  Let left child represent "next" pattern, and right as "child" pattern.
 *
 *  Then we can convert the above into a textual representation as follows:
 *    {a}
 *      {b}
 *        {c}
 *        {d}
 *      {e}
 *    {f}
 *    {g}
 *
 *  {a} is the root pattern with child pattern {b} (note: 2 space indent)
 *  and next pattern {f} (same indent).
 *  {b} has child {c} and next pattern {e}.
 *  {c} has next pattern {d}.
 *  {f} has next pattern {g}.
 *
 *  In the representation {a} is expanded to all the pattern fields.
 */
int canonical_pattern(char *s, struct PatternHead pat, int indent)
{
  char *p = s;

  char space[64] = { 0 };
  for (int i = 0; i < indent; i++)
  {
    space[2 * i] = ' ';
    space[2 * i + 1] = ' ';
  }

  struct Pattern *e;

  p += sprintf(p, "");

  SLIST_FOREACH(e, &pat, entries)
  {
    p += sprintf(p, "%s", space);
    p += sprintf(p, "{");
    p += sprintf(p, "%d,", e->op);
    p += sprintf(p, "%d,", e->not);
    p += sprintf(p, "%d,", e->alladdr);
    p += sprintf(p, "%d,", e->stringmatch);
    p += sprintf(p, "%d,", e->groupmatch);
    p += sprintf(p, "%d,", e->ign_case);
    p += sprintf(p, "%d,", e->isalias);
    p += sprintf(p, "%d,", e->ismulti);
    p += sprintf(p, "%d,", e->min);
    p += sprintf(p, "%d,", e->max);
    p += sprintf(p, "\"%s\",", e->p.str ? e->p.str : "");
    p += sprintf(p, "%s,", SLIST_EMPTY(&e->child) ? "(null)" : "(list)");
    p += sprintf(p, "%s", SLIST_NEXT(e, entries) ? "(next)" : "(null)");
    p += sprintf(p, "}\n");

    if (!SLIST_EMPTY(&e->child))
      p += canonical_pattern(p, e->child, indent + 1);
  }

  return p - s;
}

/* best-effort pattern tree compare, returns 0 if equal otherwise 1 */
static int cmp_pattern(struct PatternHead p1, struct PatternHead p2)
{
  if (SLIST_EMPTY(&p1) || SLIST_EMPTY(&p2))
  {
    return !(SLIST_EMPTY(&p1) && SLIST_EMPTY(&p2));
  }

  while (!SLIST_EMPTY(&p1))
  {
    struct Pattern *l, *r;

    l = SLIST_FIRST(&p1);
    r = SLIST_FIRST(&p2);

    /* if l is NULL then r must be NULL (and vice-versa) */
    if ((!l || !r) && !(!l && !r))
      return 1;

    SLIST_REMOVE_HEAD(&p1, entries);
    SLIST_REMOVE_HEAD(&p2, entries);

    if (l->op != r->op)
      return 1;
    if (l->not != r->not)
      return 1;
    if (l->alladdr != r->alladdr)
      return 1;
    if (l->stringmatch != r->stringmatch)
      return 1;
    if (l->groupmatch != r->groupmatch)
      return 1;
    if (l->ign_case != r->ign_case)
      return 1;
    if (l->isalias != r->isalias)
      return 1;
    if (l->ismulti != r->ismulti)
      return 1;
    if (l->min != r->min)
      return 1;
    if (l->max != r->max)
      return 1;

    if (l->stringmatch && strcmp(l->p.str, r->p.str))
      return 1;

    if (cmp_pattern(l->child, r->child))
      return 1;
  }

  return 0;
}

void test_mutt_pattern_comp(void)
{
  struct Buffer err;
  struct PatternHead pat;

  err.dsize = 1024;
  err.data = mutt_mem_malloc(err.dsize);

  { /* empty */
    char *s = "";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <empty>");
      TEST_MSG("Actual  : pat == <non-empty>");
    }

    char *msg = "empty pattern";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  { /* invalid */
    char *s = "x";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <empty>");
      TEST_MSG("Actual  : pat == <not-empty>");
    }

    char *msg = "error in pattern at: x";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  { /* missing parameter */
    char *s = "=s";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <empty>");
      TEST_MSG("Actual  : pat == <not-empty>");
    }

    char *msg = "missing parameter";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  { /* error in pattern */
    char *s = "| =s foo";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <empty>");
      TEST_MSG("Actual  : pat == <not-empty>");
    }

    char *msg = "error in pattern at: | =s foo";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  {
    char *s = "=s foobar";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(!SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <not-empty>");
      TEST_MSG("Actual  : pat == <empty>");
    }

    struct PatternHead expected;
    SLIST_INIT(&expected);
    struct Pattern e = { .op = 30 /* MUTT_SUBJECT */,
                         .not = 0,
                         .alladdr = 0,
                         .stringmatch = 1,
                         .groupmatch = 0,
                         .ign_case = 1,
                         .isalias = 0,
                         .ismulti = 0,
                         .min = 0,
                         .max = 0,
                         .p.str = "foobar" };
    SLIST_INSERT_HEAD(&expected, &e, entries);

    if (!TEST_CHECK(!cmp_pattern(pat, expected)))
    {
      char s[1024];
      canonical_pattern(s, expected, 0);
      TEST_MSG("Expected:\n%s", s);
      canonical_pattern(s, pat, 0);
      TEST_MSG("Actual:\n%s", s);
    }

    char *msg = "";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  {
    char *s = "! =s foobar";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(!SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <not-empty>");
      TEST_MSG("Actual  : pat == <empty>");
    }

    struct PatternHead expected;
    SLIST_INIT(&expected);
    struct Pattern e = { .op = 30 /* MUTT_SUBJECT */,
                         .not = 1,
                         .alladdr = 0,
                         .stringmatch = 1,
                         .groupmatch = 0,
                         .ign_case = 1,
                         .isalias = 0,
                         .ismulti = 0,
                         .min = 0,
                         .max = 0,
                         .p.str = "foobar" };
    SLIST_INIT(&e.child);
    SLIST_INSERT_HEAD(&expected, &e, entries);

    if (!TEST_CHECK(!cmp_pattern(pat, expected)))
    {
      char s[1024];
      canonical_pattern(s, expected, 0);
      TEST_MSG("Expected:\n%s", s);
      canonical_pattern(s, pat, 0);
      TEST_MSG("Actual:\n%s", s);
    }

    char *msg = "";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  {
    char *s = "=s foo =s bar";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(!SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <not-empty>");
      TEST_MSG("Actual  : pat == <empty>");
    }

    struct PatternHead expected;

    struct Pattern e[3] = { /* root */
                            { .op = 22 /* MUTT_AND */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 0,
                              .groupmatch = 0,
                              .ign_case = 0,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = NULL },
                            /* root->child */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "foo" },
                            /* root->child->next */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "bar" }
    };

    SLIST_INIT(&expected);
    SLIST_INSERT_HEAD(&expected, &e[0], entries);
    SLIST_INSERT_HEAD(&e[0].child, &e[1], entries);
    SLIST_INSERT_AFTER(&e[1], &e[2], entries);

    if (!TEST_CHECK(!cmp_pattern(pat, expected)))
    {
      char s[1024];
      canonical_pattern(s, expected, 0);
      TEST_MSG("Expected:\n%s", s);
      canonical_pattern(s, pat, 0);
      TEST_MSG("Actual:\n%s", s);
    }

    char *msg = "";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  {
    char *s = "(=s foo =s bar)";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(!SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <not-empty>");
      TEST_MSG("Actual  : pat == <empty>");
    }

    struct PatternHead expected;

    struct Pattern e[3] = { /* root */
                            { .op = 22 /* MUTT_AND */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 0,
                              .groupmatch = 0,
                              .ign_case = 0,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = NULL },
                            /* root->child */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "foo" },
                            /* root->child->next */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "bar" }
    };

    SLIST_INIT(&expected);
    SLIST_INSERT_HEAD(&expected, &e[0], entries);
    SLIST_INSERT_HEAD(&e[0].child, &e[1], entries);
    SLIST_INSERT_AFTER(&e[1], &e[2], entries);

    if (!TEST_CHECK(!cmp_pattern(pat, expected)))
    {
      char s[1024];
      canonical_pattern(s, expected, 0);
      TEST_MSG("Expected:\n%s", s);
      canonical_pattern(s, pat, 0);
      TEST_MSG("Actual:\n%s", s);
    }

    char *msg = "";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  {
    char *s = "! (=s foo =s bar)";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(!SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <not-empty>");
      TEST_MSG("Actual  : pat == <empty>");
    }

    struct PatternHead expected;

    struct Pattern e[3] = { /* root */
                            { .op = 22 /* MUTT_AND */,
                              .not = 1,
                              .alladdr = 0,
                              .stringmatch = 0,
                              .groupmatch = 0,
                              .ign_case = 0,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = NULL },
                            /* root->child */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "foo" },
                            /* root->child->next */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "bar" }
    };

    SLIST_INIT(&expected);
    SLIST_INSERT_HEAD(&expected, &e[0], entries);
    SLIST_INSERT_HEAD(&e[0].child, &e[1], entries);
    SLIST_INSERT_AFTER(&e[1], &e[2], entries);

    if (!TEST_CHECK(!cmp_pattern(pat, expected)))
    {
      char s[1024];
      canonical_pattern(s, expected, 0);
      TEST_MSG("Expected:\n%s", s);
      canonical_pattern(s, pat, 0);
      TEST_MSG("Actual:\n%s", s);
    }

    char *msg = "";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  {
    char *s = "=s foo =s bar =s quux";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(!SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <not-empty>");
      TEST_MSG("Actual  : pat == <empty>");
    }

    struct PatternHead expected;

    struct Pattern e[4] = { /* root */
                            { .op = 22 /* MUTT_AND */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 0,
                              .groupmatch = 0,
                              .ign_case = 0,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = NULL },
                            /* root->child */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "foo" },
                            /* root->child->next */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "bar" },
                            /* root->child->next->next */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "quux" }
    };

    SLIST_INIT(&expected);
    SLIST_INSERT_HEAD(&expected, &e[0], entries);
    SLIST_INSERT_HEAD(&e[0].child, &e[1], entries);
    SLIST_INSERT_AFTER(&e[1], &e[2], entries);
    SLIST_INSERT_AFTER(&e[2], &e[3], entries);

    if (!TEST_CHECK(!cmp_pattern(pat, expected)))
    {
      char s[1024];
      canonical_pattern(s, expected, 0);
      TEST_MSG("Expected:\n%s", s);
      canonical_pattern(s, pat, 0);
      TEST_MSG("Actual:\n%s", s);
    }

    char *msg = "";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  {
    char *s = "!(=s foo|=s bar) =s quux";

    mutt_buffer_reset(&err);
    pat = mutt_pattern_comp(s, 0, &err);

    if (!TEST_CHECK(!SLIST_EMPTY(&pat)))
    {
      TEST_MSG("Expected: pat == <not-empty>");
      TEST_MSG("Actual  : pat == <empty>");
    }

    struct PatternHead expected;

    struct Pattern e[5] = { /* root */
                            { .op = 22 /* MUTT_AND */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 0,
                              .groupmatch = 0,
                              .ign_case = 0,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = NULL },
                            /* root->child */
                            { .op = 23 /* MUTT_OR */,
                              .not = 1,
                              .alladdr = 0,
                              .stringmatch = 0,
                              .groupmatch = 0,
                              .ign_case = 0,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = NULL },
                            /* root->child->child */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "foo" },
                            /* root->child->child->next */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "bar" },
                            /* root->child->next */
                            { .op = 30 /* MUTT_SUBJECT */,
                              .not = 0,
                              .alladdr = 0,
                              .stringmatch = 1,
                              .groupmatch = 0,
                              .ign_case = 1,
                              .isalias = 0,
                              .ismulti = 0,
                              .min = 0,
                              .max = 0,
                              .p.str = "quux" }
    };

    SLIST_INIT(&expected);
    SLIST_INSERT_HEAD(&expected, &e[0], entries);
    SLIST_INSERT_HEAD(&e[0].child, &e[1], entries);
    SLIST_INSERT_HEAD(&e[1].child, &e[2], entries);
    SLIST_INSERT_AFTER(&e[2], &e[3], entries);
    SLIST_INSERT_AFTER(&e[1], &e[4], entries);

    if (!TEST_CHECK(!cmp_pattern(pat, expected)))
    {
      char s[1024];
      canonical_pattern(s, expected, 0);
      TEST_MSG("Expected:\n%s", s);
      canonical_pattern(s, pat, 0);
      TEST_MSG("Actual:\n%s", s);
    }

    char *msg = "";
    if (!TEST_CHECK(!strcmp(err.data, msg)))
    {
      TEST_MSG("Expected: %s", msg);
      TEST_MSG("Actual  : %s", err.data);
    }

    mutt_pattern_free(&pat);
  }

  FREE(&err.data);
}
