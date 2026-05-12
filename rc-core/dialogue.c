#include "dialogue.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DLGX_MAGIC 0x58474C44u
#define DLGX_VERSION 1u

RcDialogueTranscriptDef *g_rc_dialogue_transcripts = NULL;
RcDialogueDefNode *g_rc_dialogue_nodes = NULL;
uint16_t *g_rc_dialogue_child_ids = NULL;
uint32_t *g_rc_dialogue_npc_name_offsets = NULL;
int g_rc_dialogue_transcript_count = 0;
int g_rc_dialogue_node_count = 0;
int g_rc_dialogue_child_id_count = 0;

static char *g_dialogue_strings = NULL;
static uint32_t g_dialogue_string_count = 1;

typedef struct {
    char *data;
    uint32_t count;
    uint32_t cap;
} RcDialogueStringBlob;

static const char *blob_string(const RcDialogueStringBlob *blob,
                               uint32_t offset) {
    if (!blob || !blob->data || offset >= blob->count) return "";
    return &blob->data[offset];
}

static int append_string(RcDialogueStringBlob *blob, const char *src, int len,
                         uint32_t *offset) {
    if (!blob->data) {
        blob->cap = 4096;
        blob->data = calloc(blob->cap, 1);
        if (!blob->data) return 0;
        blob->count = 1;
    }
    uint32_t need = blob->count + (uint32_t)len + 1u;
    if (need > blob->cap) {
        uint32_t next_cap = blob->cap;
        while (next_cap < need) next_cap *= 2u;
        char *next = realloc(blob->data, next_cap);
        if (!next) return 0;
        blob->data = next;
        blob->cap = next_cap;
    }
    *offset = blob->count;
    memcpy(&blob->data[blob->count], src, (size_t)len);
    blob->count += (uint32_t)len;
    blob->data[blob->count++] = '\0';
    return 1;
}

static int read_blob_string(FILE *f, const char *path,
                            RcDialogueStringBlob *blob, int len_bytes,
                            uint32_t *offset) {
    uint32_t len = 0;
    if (len_bytes == 1) {
        uint8_t n;
        if (!rc_read_exact(f, &n, sizeof(n), 1, path, "string len")) return 0;
        len = n;
    } else {
        uint16_t n;
        if (!rc_read_exact(f, &n, sizeof(n), 1, path, "string len")) return 0;
        len = n;
    }
    char stack[512];
    char *buf = stack;
    if (len + 1u > sizeof(stack)) {
        buf = malloc((size_t)len + 1u);
        if (!buf) return 0;
    }
    if (len && !rc_read_exact(f, buf, 1, len, path, "string")) {
        if (buf != stack) free(buf);
        return 0;
    }
    buf[len] = '\0';
    int ok = append_string(blob, buf, (int)len, offset);
    if (buf != stack) free(buf);
    return ok;
}

static int read_short_name(FILE *f, const char *path,
                           RcDialogueStringBlob *blob, char *out, int cap) {
    uint32_t off;
    if (!read_blob_string(f, path, blob, 1, &off)) return 0;
    snprintf(out, (size_t)cap, "%s", blob_string(blob, off));
    return 1;
}

static void free_dialogue_load(RcDialogueTranscriptDef *transcripts,
                               RcDialogueDefNode *nodes,
                               uint16_t *children,
                               uint32_t *npc_names,
                               RcDialogueStringBlob *strings) {
    free(transcripts);
    free(nodes);
    free(children);
    free(npc_names);
    free(strings->data);
    strings->data = NULL;
    strings->count = 1;
    strings->cap = 0;
}

int rc_load_dialogue(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, transcript_count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path,
                              "version")
            || !rc_read_exact(f, &transcript_count, sizeof(transcript_count),
                              1, path, "transcript count")
            || magic != DLGX_MAGIC || version != DLGX_VERSION) {
        fclose(f);
        return -1;
    }

    RcDialogueTranscriptDef *transcripts =
        calloc(transcript_count ? transcript_count : 1u, sizeof(*transcripts));
    RcDialogueDefNode *nodes = NULL;
    uint16_t *children = NULL;
    uint32_t *npc_names = NULL;
    int node_count = 0, node_cap = 0;
    int child_count = 0, child_cap = 0;
    int npc_count = 0, npc_cap = 0;
    RcDialogueStringBlob strings = {0};
    if (!transcripts) {
        fclose(f);
        return -1;
    }

    for (uint32_t i = 0; i < transcript_count; i++) {
        RcDialogueTranscriptDef *t = &transcripts[i];
        if (!read_short_name(f, path, &strings, t->slug, sizeof(t->slug))) {
            free_dialogue_load(transcripts, nodes, children, npc_names,
                               &strings);
            fclose(f); return -1;
        }
        uint8_t n_npcs;
        if (!rc_read_exact(f, &n_npcs, sizeof(n_npcs), 1, path, "npc count")) {
            free_dialogue_load(transcripts, nodes, children, npc_names,
                               &strings);
            fclose(f); return -1;
        }
        if (npc_count + n_npcs > npc_cap) {
            int next_cap = npc_cap ? npc_cap * 2 : 512;
            while (next_cap < npc_count + n_npcs) next_cap *= 2;
            uint32_t *next = realloc(npc_names,
                                     (size_t)next_cap * sizeof(*npc_names));
            if (!next) {
                free_dialogue_load(transcripts, nodes, children, npc_names,
                                   &strings);
                fclose(f); return -1;
            }
            npc_names = next;
            npc_cap = next_cap;
        }
        t->first_npc = (uint32_t)npc_count;
        t->npc_count = n_npcs;
        for (int n = 0; n < n_npcs; n++) {
            if (!read_blob_string(f, path, &strings, 1,
                                  &npc_names[npc_count++])) {
                free_dialogue_load(transcripts, nodes, children, npc_names,
                                   &strings);
                fclose(f); return -1;
            }
        }
        uint16_t n_nodes;
        if (!rc_read_exact(f, &n_nodes, sizeof(n_nodes), 1, path,
                           "node count")) {
            free_dialogue_load(transcripts, nodes, children, npc_names,
                               &strings);
            fclose(f); return -1;
        }
        if (node_count + n_nodes > node_cap) {
            int next_cap = node_cap ? node_cap * 2 : 4096;
            while (next_cap < node_count + n_nodes) next_cap *= 2;
            RcDialogueDefNode *next =
                realloc(nodes, (size_t)next_cap * sizeof(*nodes));
            if (!next) {
                free_dialogue_load(transcripts, nodes, children, npc_names,
                                   &strings);
                fclose(f); return -1;
            }
            nodes = next;
            node_cap = next_cap;
        }
        t->first_node = (uint32_t)node_count;
        t->node_count = n_nodes;
        for (int n = 0; n < n_nodes; n++) {
            RcDialogueDefNode *node = &nodes[node_count++];
            if (!rc_read_exact(f, &node->id, sizeof(node->id), 1, path,
                               "node id")
                    || !rc_read_exact(f, &node->parent, sizeof(node->parent),
                                      1, path, "parent")
                    || !rc_read_exact(f, &node->depth, sizeof(node->depth), 1,
                                      path, "depth")
                    || !rc_read_exact(f, &node->kind, sizeof(node->kind), 1,
                                      path, "kind")
                    || !rc_read_exact(f, &node->is_terminal,
                                      sizeof(node->is_terminal), 1, path,
                                      "terminal")
                    || !read_blob_string(f, path, &strings, 1,
                                        &node->speaker_off)
                    || !read_blob_string(f, path, &strings, 2,
                                        &node->text_off)
                    || !rc_read_exact(f, &node->child_count,
                                      sizeof(node->child_count), 1, path,
                                      "child count")) {
                free_dialogue_load(transcripts, nodes, children, npc_names,
                                   &strings);
                fclose(f); return -1;
            }
            if (child_count + node->child_count > child_cap) {
                int next_cap = child_cap ? child_cap * 2 : 4096;
                while (next_cap < child_count + node->child_count) {
                    next_cap *= 2;
                }
                uint16_t *next =
                    realloc(children, (size_t)next_cap * sizeof(*children));
                if (!next) {
                    free_dialogue_load(transcripts, nodes, children,
                                       npc_names, &strings);
                    fclose(f); return -1;
                }
                children = next;
                child_cap = next_cap;
            }
            node->first_child = (uint32_t)child_count;
            for (int c = 0; c < node->child_count; c++) {
                if (!rc_read_exact(f, &children[child_count++],
                                   sizeof(*children), 1, path, "child id")) {
                    free_dialogue_load(transcripts, nodes, children,
                                       npc_names, &strings);
                    fclose(f); return -1;
                }
            }
        }
    }
    fclose(f);
    free(g_rc_dialogue_transcripts);
    free(g_rc_dialogue_nodes);
    free(g_rc_dialogue_child_ids);
    free(g_rc_dialogue_npc_name_offsets);
    free(g_dialogue_strings);
    g_rc_dialogue_transcripts = transcripts;
    g_rc_dialogue_nodes = nodes;
    g_rc_dialogue_child_ids = children;
    g_rc_dialogue_npc_name_offsets = npc_names;
    g_dialogue_strings = strings.data;
    g_dialogue_string_count = strings.count;
    g_rc_dialogue_transcript_count = (int)transcript_count;
    g_rc_dialogue_node_count = node_count;
    g_rc_dialogue_child_id_count = child_count;
    return g_rc_dialogue_transcript_count;
}

int rc_dialogue_find_transcript(const char *slug) {
    if (!slug || !g_rc_dialogue_transcripts) return -1;
    for (int i = 0; i < g_rc_dialogue_transcript_count; i++) {
        if (strcmp(g_rc_dialogue_transcripts[i].slug, slug) == 0) return i;
    }
    return -1;
}

const RcDialogueTranscriptDef *rc_dialogue_transcript_get(int idx) {
    if (idx < 0 || idx >= g_rc_dialogue_transcript_count) return NULL;
    return &g_rc_dialogue_transcripts[idx];
}

const RcDialogueDefNode *rc_dialogue_nodes_for(
    const RcDialogueTranscriptDef *transcript, int *count) {
    if (count) *count = 0;
    if (!transcript || !g_rc_dialogue_nodes) return NULL;
    if (count) *count = transcript->node_count;
    return &g_rc_dialogue_nodes[transcript->first_node];
}

const char *rc_dialogue_string(uint32_t offset) {
    if (!g_dialogue_strings || offset >= g_dialogue_string_count) return "";
    return &g_dialogue_strings[offset];
}

const char *rc_dialogue_transcript_npc(const RcDialogueTranscriptDef *row,
                                       int idx) {
    if (!row || idx < 0 || idx >= row->npc_count
            || !g_rc_dialogue_npc_name_offsets) {
        return "";
    }
    return rc_dialogue_string(g_rc_dialogue_npc_name_offsets[row->first_npc
                                                            + (uint32_t)idx]);
}

void rc_dialogue_start(RcWorld *world, int npc_uid,
                       const RcDialogueNode *nodes, int count) {
    // TODO: set dialogue state
    (void)world; (void)npc_uid; (void)nodes; (void)count;
}

void rc_dialogue_continue(RcWorld *world) {
    (void)world;
}

void rc_dialogue_choose(RcWorld *world, int option) {
    (void)world; (void)option;
}
