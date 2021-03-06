#include <stdio.h>
#include <string.h>
#include <alloca.h>
#include <assert.h>
#include <unistd.h>

#include <Python.h>
#include <apr_general.h>
#include <apr_thread_proc.h>
#include <apr_time.h>
#include <apr_hash.h>

#include "lib/helpers.h"
#include "lib/pyhelpers.h"
#include "lib/protopy.lex.h"
#include "lib/protopy.tab.h"
#include "lib/binparser.h"
#include "lib/defparser.h"
#include "lib/list.h"
#include "lib/descriptors.h"


static char module_docstring[] = "Protobuf decoder and encoder.";
static char parse_docstring[] = "Parse binary Protobuf message.";
static char parse_def_docstring[] = "Parse Protobuf definition.";
static char apr_cleanup_docstring[] = "Calls apr_terminate().";
static char make_state_docstring[] = "Creates state capsule.";
static char state_ready_docstring[] = "Returns True when state finished parsing.";
static char state_result_docstring[] = "The object that was deserialized.";
static char make_apr_pool_docstring[] = "Create a new APR memory pool";
static char apr_update_hash_docstring[] = "Merge two APR hash tables";
static char apr_hash_contains_docstring[] = "Check if APR hash table contains a key";
static char make_apr_hash_docstring[] = "Create new APR hash table";
static char apr_hash_iterator_docstring[] = "Create new iterator for APR hash table";
static char apr_hash_get_docstring[] = "Fetch next pair from APR hash table";

static PyObject* proto_parse(PyObject* self, PyObject* args);
static PyObject* proto_def_parse(PyObject* self, PyObject* args);
static PyObject* apr_cleanup(PyObject* self, PyObject* args);
static PyObject* make_apr_pool(PyObject* self, PyObject* args);
static PyObject* apr_update_hash(PyObject* self, PyObject* args);
static PyObject* apr_hash_contains(PyObject* self, PyObject* args);
static PyObject* make_apr_hash(PyObject* self, PyObject* args);
static PyObject* apr_hash_iterator(PyObject* self, PyObject* args);
static PyObject* apr_hash_get_kv(PyObject* self, PyObject* args);

static PyMethodDef module_methods[] = {
    {"proto_parse", proto_parse, METH_VARARGS, parse_docstring},
    {"proto_def_parse", proto_def_parse, METH_VARARGS, parse_def_docstring},
    {"apr_cleanup", apr_cleanup, METH_VARARGS, apr_cleanup_docstring},
    {"make_state", make_state, METH_VARARGS, make_state_docstring},
    {"state_ready", state_ready, METH_VARARGS, state_ready_docstring},
    {"state_result", state_result, METH_VARARGS, state_result_docstring},
    {"make_apr_pool", make_apr_pool, METH_VARARGS, make_apr_pool_docstring},
    {"apr_update_hash", apr_update_hash, METH_VARARGS, apr_update_hash_docstring},
    {"apr_hash_contains", apr_hash_contains, METH_VARARGS, apr_hash_contains_docstring},
    {"make_apr_hash", make_apr_hash, METH_VARARGS, make_apr_hash_docstring},
    {"apr_hash_iterator", apr_hash_iterator, METH_VARARGS, apr_hash_iterator_docstring},
    {"apr_hash_get_kv", apr_hash_get_kv, METH_VARARGS, apr_hash_get_docstring},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef protopy_module = {
    PyModuleDef_HEAD_INIT,
    "protopy.wrapped",
    module_docstring, /* m_doc */
    -1, /* m_size */
    module_methods /* m_methods */
};

PyMODINIT_FUNC PyInit_wrapped(void);
PyMODINIT_FUNC PyInit_wrapped(void) {
    Py_Initialize();
    apr_initialize();
    return PyModule_Create(&protopy_module);
}

static PyObject* apr_cleanup(PyObject* self, PyObject* args) {
    apr_terminate();
    Py_RETURN_NONE;
}

void free_apr_hash(PyObject* capsule) { }

static PyObject* apr_hash_iterator(PyObject* self, PyObject* args) {
    PyObject* capsule;

    if (!PyArg_ParseTuple(args, "O", &capsule)) {
        return NULL;
    }
    apr_hash_t* ht = (apr_hash_t*)PyCapsule_GetPointer(capsule, NULL);
    if (!ht) {
        PyErr_Format(PyExc_ValueError, "Missing hash table");
        return NULL;
    }
    apr_pool_t* mp = apr_hash_pool_get(ht);
    apr_hash_index_t* hi = apr_hash_first(mp, ht);
    return PyCapsule_New(hi, NULL, free_apr_hash);
}

static PyObject* apr_hash_get_kv(PyObject* self, PyObject* args) {
    PyObject* capsule;

    if (!PyArg_ParseTuple(args, "O", &capsule)) {
        return NULL;
    }
    void* maybe_hi = PyCapsule_GetPointer(capsule, NULL);
    if (!maybe_hi || maybe_hi == (void*)0xdeadbeef) {
        PyErr_SetString(PyExc_StopIteration, "");
        return NULL;
    }
    apr_hash_index_t* hi = (apr_hash_index_t*)maybe_hi;
    PyObject* result = PyTuple_New(2);

    const void* key;
    void* val;
    apr_hash_this(hi, &key, NULL, &val);
    PyObject* pykey = PyBytes_FromString((char*)key);
    PyObject* pyval = ((factory_t*)val)->ctor;
    PyTuple_SetItem(result, 0, pykey);
    PyTuple_SetItem(result, 1, pyval);
    hi = apr_hash_next(hi);
    if (!hi) {
        PyCapsule_SetPointer(capsule, (void*)0xdeadbeef);
    } else {
        PyCapsule_SetPointer(capsule, hi);
    }
    return result;
}

static PyObject* make_apr_hash(PyObject* self, PyObject* args) {
    PyObject* capsule;

    if (!PyArg_ParseTuple(args, "O", &capsule)) {
        return NULL;
    }
    apr_pool_t* mp = (apr_pool_t*)PyCapsule_GetPointer(capsule, NULL);
    if (!mp) {
        PyErr_Format(PyExc_ValueError, "Missing APR memory pool");
        return NULL;
    }
    apr_hash_t* ht = apr_hash_make(mp);
    return PyCapsule_New(ht, NULL, free_apr_hash);
}

static PyObject* apr_update_hash(PyObject* self, PyObject* args) {
    PyObject* from_capsule;
    PyObject* to_capsule;

    if (!PyArg_ParseTuple(args, "OO", &to_capsule, &from_capsule)) {
        return NULL;
    }
    apr_hash_t* from = (apr_hash_t*)PyCapsule_GetPointer(from_capsule, NULL);
    if (!from) {
        PyErr_Format(PyExc_ValueError, "Missing hash table to update from");
        return NULL;
    }
    apr_hash_t* to = (apr_hash_t*)PyCapsule_GetPointer(to_capsule, NULL);
    if (!from) {
        PyErr_Format(PyExc_ValueError, "Missing hash table to update");
        return NULL;
    }
    apr_pool_t* mp = apr_hash_pool_get(to);
    apr_hash_t* result = apr_hash_overlay(mp, from, to);
    PyCapsule_SetPointer(to_capsule, result);
    Py_RETURN_NONE;
}

static PyObject* apr_hash_contains(PyObject* self, PyObject* args) {
    PyObject* capsule;
    char* key;

    if (!PyArg_ParseTuple(args, "Oy", &capsule, &key)) {
        return NULL;
    }
    apr_hash_t* ht = (apr_hash_t*)PyCapsule_GetPointer(capsule, NULL);
    if (!ht) {
        PyErr_Format(PyExc_ValueError, "Missing hash table");
        return NULL;
    }
    byte* apr_key = cstr_bytes(key);
    void* result = apr_hash_get(ht, apr_key, str_size(key) + 2);
    if (result) {
        free(apr_key);
        Py_INCREF(Py_True);
        return Py_True;
    }
    free(apr_key);
    Py_INCREF(Py_False);
    return Py_False;
}

void free_apr_pool(PyObject* capsule) {
    if (capsule == Py_None) {
        return;
    }
    apr_pool_t* mp = (apr_pool_t*)PyCapsule_GetPointer(capsule, NULL);
    if (mp) {
        apr_pool_destroy(mp);
    }
}

static PyObject* make_apr_pool(PyObject* self, PyObject* args) {
    apr_pool_t* mp;
    if (apr_pool_create(&mp, NULL) != APR_SUCCESS) {
        PyErr_SetString(PyExc_TypeError, "Couldn't create memory pool");
        return NULL;
    }
    return PyCapsule_New(mp, NULL, free_apr_pool);
}

static PyObject* proto_parse(PyObject* self, PyObject* args) {
    parse_state_t* state;
    PyObject* in;
    PyObject* capsule;
    
    if (!PyArg_ParseTuple(args, "OO", &in, &capsule)) {
        return NULL;
    }
    state = (parse_state_t*)PyCapsule_GetPointer(capsule, NULL);
    if (state == NULL) {
        return NULL;
    }
    Py_buffer buf;
    unsigned char* contiguous = NULL;
    if (PyObject_GetBuffer(in, &buf, PyBUF_SIMPLE)) {
        PyErr_Format(PyExc_TypeError, "%A doesn't implement buffer API", in);
        return NULL;
    }
    if (!PyBuffer_IsContiguous(&buf, 'C')) {
        if (PyBuffer_ToContiguous(contiguous, &buf, buf.len, 'C')) {
            PyErr_Format(PyExc_MemoryError, "cannot copy %i bytes from %A", buf.len, in);
            return NULL;
        }
        state->in = contiguous;
    } else {
        state->in = (unsigned char*)buf.buf;
    }
    state->len = (int64_t)buf.len;
    PyObject* result = parse_message(state);
    PyBuffer_Release(&buf);
    return result;
}

size_t available_thread_pos(parsing_progress_t* progress) {
    size_t i = 0;
    while (i < progress->nthreads) {
        if (!progress->thds_statuses[i] && !progress->thds[i]) {
            break;
        }
        i++;
    }
    return i;
}

size_t finished_thread(parsing_progress_t* progress) {
    size_t i = 0;
    while (i < progress->nthreads) {
        if (!progress->thds_statuses[i] && progress->thds[i]) {
            break;
        }
        i++;
    }
    return i;
}

void start_progress(parsing_progress_t* progress, size_t nthreads, apr_pool_t* mp) {
    size_t i = 0;

    progress->nthreads = nthreads;
    progress->thds_statuses = malloc(sizeof(bool) * nthreads);
    progress->thds = malloc(sizeof(void*) * nthreads);
    
    while (i < nthreads) {
        progress->thds_statuses[i] = false;
        progress->thds[i] = NULL;
        i++;
    }
}

void finish_progress(parsing_progress_t* progress) {
    free(progress->thds_statuses);
    free(progress->thds);
}

bool all_threads_finished(parsing_progress_t* progress) {
    size_t i = 0;
    while (i < progress->nthreads) {
        if (progress->thds_statuses[i] || progress->thds[i] != NULL) {
            return false;
        }
        i++;
    }
    return true;
}

bool all_threads_busy(parsing_progress_t* progress) {
    return available_thread_pos(progress) < progress->nthreads;
}

apr_hash_t* built_in_types(apr_pool_t* mp) {
    apr_hash_t* result = apr_hash_make(mp);
    static char* builtins[] = {
        "int32",
        "int64",
        "uint32",
        "uint64",
        "sint32",
        "sint64",
        "bool",
        "fixed64",
        "sfixed64",
        "double",
        "fixed32",
        "sfixed32",
        "float",
        "string",
        "bytes"
    };
    size_t i;
    for (i = 0; i < 15; i++) {
        apr_hash_set(result, builtins[i], strlen(builtins[i]), (void*)true);
    }
    return result;
}

void
start_defparser_thread(
    parsing_progress_t* progress,
    parse_def_args_t** thds_args,
    size_t i,
    char* source,
    list roots,
    apr_pool_t* mp) {

    parse_def_args_t* def_args = thds_args[i];

    def_args->roots = roots;
    def_args->source = source;
    def_args->error = NULL;
    def_args->result = NULL;
    def_args->thread_id = i;
    def_args->progress = progress;
    thds_args[i] = def_args;
    progress->thds_statuses[i] = true;
    apr_thread_create(&progress->thds[i], NULL, parse_one_def, def_args, mp);
}

list
process_finished_threads(
    parsing_progress_t* progress,
    parse_def_args_t** thds_args,
    apr_hash_t* defs,
    apr_hash_t* builtins,
    error_info_t* einfo,
    apr_pool_t* mp) {

    size_t i = finished_thread(progress);
    list deps = nil;

    if (i < progress->nthreads) {
        if (thds_args[i]->error != NULL) {
            einfo->message = thds_args[i]->error;
            einfo->kind = thds_args[i]->error_kind;
            apr_status_t rv;
            apr_thread_join(&rv, progress->thds[i]);
            progress->thds[i] = NULL;

            size_t j;
            for (j = 0; j < progress->nthreads; j++) {
                if (progress->thds[j] != NULL) {
                    apr_thread_join(&rv, progress->thds[j]);
                }
            }
            return nil;
        }
        apr_status_t rv;
        apr_thread_join(&rv, progress->thds[i]);
        progress->thds[i] = NULL;
        deps = imports(thds_args[i]->result);
        apr_hash_t* declarations = apr_hash_make(mp);
        list normalized = normalize_messages(thds_args[i]->result, mp);
        collect_declarations(normalized, declarations);
        list parsed = normalize_types(normalized, declarations, builtins, deps, mp);
        apr_hash_set(defs, thds_args[i]->source, APR_HASH_KEY_STRING, parsed);
    }
    return deps;
}

static apr_hash_t*
proto_def_parse_produce(
    list sources,
    list roots,
    size_t nthreads,
    apr_pool_t* mp,
    error_info_t* einfo) {

    parsing_progress_t progress;
    size_t i = 0;
    parse_def_args_t** thds_args = alloca(sizeof(parse_def_args_t*) * nthreads);
    apr_hash_t* result = apr_hash_make(mp);
    apr_hash_t* builtins = built_in_types(mp);

    while (i < nthreads) {
        thds_args[i] = malloc(sizeof(parse_def_args_t));
        i++;
    }

    start_progress(&progress, nthreads, mp);

    while (!null(sources) || !all_threads_finished(&progress)) {

        i = available_thread_pos(&progress);

        while (!null(sources) && i < progress.nthreads) {
            char* source = bytes_cstr(car(sources));
            sources = cdr(sources);
            if (!apr_hash_get(result, source, APR_HASH_KEY_STRING)) {
                apr_hash_set(result, source, APR_HASH_KEY_STRING, (void*)true);
                start_defparser_thread(&progress, thds_args, i, source, roots, mp);
            } else {
                free(source);
            }
            i = available_thread_pos(&progress);
        }
        list extras = process_finished_threads(
            &progress,
            thds_args,
            result,
            builtins,
            einfo,
            mp);
        if (einfo->message) {
            del(sources);
            break;
        }
        list new_sources = append(sources, extras);
        del(sources);
        sources = new_sources;
        if (null(sources) || all_threads_busy(&progress)) {
            apr_sleep(100);
        }
    }

    finish_progress(&progress);

    return result;
}

PyObject* aprdict_to_pydict(apr_pool_t* mp, apr_hash_t* ht) {
    PyObject* result = PyDict_New();
    apr_hash_index_t* hi;
    void* val;
    const void* key;

    for (hi = apr_hash_first(mp, ht); hi; hi = apr_hash_next(hi)) {
        apr_hash_this(hi, &key, NULL, &val);
        PyObject* pykey = PyBytes_FromString((char*)key);
        PyDict_SetItem(result, pykey, list_to_pylist((list)val));
        Py_DECREF(pykey);
    }
    return result;
}

static PyObject* proto_def_parse(PyObject* self, PyObject* args) {
    PyObject* source_roots;
    PyObject* parsed_files;
    PyObject* message_ctor;
    PyObject* enum_ctor;
    PyObject* pykeywords;
    PyObject* mp_capsule;
    char* source;

    if (!PyArg_ParseTuple(
            args,
            "yO!O!OOOO",
            &source,
            &PyList_Type,
            &source_roots,
            &PyDict_Type,
            &parsed_files,
            &message_ctor,
            &enum_ctor,
            &pykeywords,
            &mp_capsule)) {
        return NULL;
    }

    PyObject* multiprocessing = PyImport_ImportModule("multiprocessing");
    PyObject* ncores = PyObject_CallMethod(multiprocessing, "cpu_count", "");
    int nthreads;
    PyArg_Parse(ncores, "i", &nthreads);
    if (nthreads < 1) {
        nthreads = 1;
    }
    Py_DECREF(multiprocessing);
    Py_DECREF(ncores);
    list roots = pylist_to_list(source_roots);
    // TODO(olegs): This pool needs to be initialized per parser
    // instance.
    apr_pool_t* mp = PyCapsule_GetPointer(mp_capsule, NULL);
    if (!mp) {
        PyErr_Format(PyExc_ValueError, "APR memory pool wasn't initialized");
        return NULL;
    }
    apr_hash_t* parsed_defs;

    error_info_t einfo;
    einfo.message = NULL;
    PyObject* error_class;

    Py_BEGIN_ALLOW_THREADS;

    list sources = cons_str(source, strlen(source), nil);
    parsed_defs = proto_def_parse_produce(sources, roots, (size_t)nthreads, mp, &einfo);

    del(roots);

    Py_END_ALLOW_THREADS;

    if (einfo.message != NULL) {
        switch (einfo.kind) {
            case fs_error:
                error_class = PyExc_FileNotFoundError;
                break;
            case parser_error:
                error_class = PyExc_SyntaxError;
                break;
            default:
                error_class = PyExc_MemoryError;
                break;
        }
        PyErr_SetString(error_class, einfo.message);
        free(einfo.message);
        return NULL;
    }

    apr_hash_t* result = create_descriptors(
        parsed_defs,
        enum_ctor,
        message_ctor,
        pykeywords,
        mp);
    if (PyErr_Occurred()) {
        return NULL;
    }
    PyObject* description = aprdict_to_pydict(mp, parsed_defs);
    PyDict_Update(parsed_files, description);
    Py_DECREF(description);
    return PyCapsule_New(result, NULL, free_apr_hash);
}
