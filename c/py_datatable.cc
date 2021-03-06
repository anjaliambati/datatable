//------------------------------------------------------------------------------
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// © H2O.ai 2018
//------------------------------------------------------------------------------
#define dt_PY_DATATABLE_cc
#include "py_datatable.h"
#include <exception>
#include <iostream>
#include <vector>
#include "datatable.h"
#include "datatable_check.h"
#include "py_column.h"
#include "py_columnset.h"
#include "py_datawindow.h"
#include "py_rowindex.h"
#include "py_types.h"
#include "py_utils.h"

namespace pydatatable
{

// Forward declarations
static PyObject* strRowIndexTypeArr32;
static PyObject* strRowIndexTypeArr64;
static PyObject* strRowIndexTypeSlice;


/**
 * Create a new `pydatatable::obj` by wrapping the provided DataTable `dt`.
 * If `dt` is nullptr then this function also returns nullptr.
 */
PyObject* wrap(DataTable* dt)
{
  if (!dt) return nullptr;
  PyObject* pytype = reinterpret_cast<PyObject*>(&pydatatable::type);
  PyObject* pydt = PyObject_CallObject(pytype, nullptr);
  if (pydt) {
    auto pypydt = reinterpret_cast<pydatatable::obj*>(pydt);
    pypydt->ref = dt;
    pypydt->use_stype_for_buffers = ST_VOID;
  }
  return pydt;
}


/**
 * Attempt to extract a DataTable from the given PyObject, iff it is a
 * pydatatable::obj. On success, the reference to the DataTable is stored in
 * the `address` and 1 is returned. On failure, returns 0.
 *
 * This function does not attempt to DECREF the source PyObject.
 */
int unwrap(PyObject* object, DataTable** address) {
  if (!object) return 0;
  if (!PyObject_TypeCheck(object, &pydatatable::type)) {
    PyErr_SetString(PyExc_TypeError, "Expected object of type DataTable");
    return 0;
  }
  *address = static_cast<pydatatable::obj*>(object)->ref;
  return 1;
}


//==============================================================================
// Generic Python API
//==============================================================================

PyObject* datatable_load(PyObject*, PyObject* args) {
  DataTable* colspec;
  int64_t nrows;
  const char* path;
  if (!PyArg_ParseTuple(args, "O&ns:datatable_load",
                        &unwrap, &colspec, &nrows, &path))
    return nullptr;
  return wrap(DataTable::load(colspec, nrows, path));
}




//==============================================================================
// PyDatatable getters/setters
//==============================================================================

PyObject* get_nrows(obj* self) {
  return PyLong_FromLongLong(self->ref->nrows);
}

PyObject* get_ncols(obj* self) {
  return PyLong_FromLongLong(self->ref->ncols);
}

PyObject* get_isview(obj* self) {
  return incref(self->ref->rowindex.isabsent()? Py_False : Py_True);
}


PyObject* get_ltypes(obj* self) {
  int64_t i = self->ref->ncols;
  PyObject* list = PyTuple_New((Py_ssize_t) i);
  if (list == nullptr) return nullptr;
  while (--i >= 0) {
    SType st = self->ref->columns[i]->stype();
    LType lt = stype_info[st].ltype;
    PyTuple_SET_ITEM(list, i, incref(py_ltype_objs[lt]));
  }
  return list;
}


PyObject* get_stypes(obj* self) {
  DataTable* dt = self->ref;
  int64_t i = dt->ncols;
  PyObject* list = PyTuple_New((Py_ssize_t) i);
  if (list == nullptr) return nullptr;
  while (--i >= 0) {
    SType st = dt->columns[i]->stype();
    PyTuple_SET_ITEM(list, i, incref(py_stype_objs[st]));
  }
  return list;
}


PyObject* get_rowindex_type(obj* self) {
  RowIndex& ri = self->ref->rowindex;
  return ri.isabsent()? none() :
         ri.isslice()? incref(strRowIndexTypeSlice) :
         ri.isarr32()? incref(strRowIndexTypeArr32) :
         ri.isarr64()? incref(strRowIndexTypeArr64) : none();
}


PyObject* get_rowindex(obj* self) {
  RowIndex& ri = self->ref->rowindex;
  return ri.isabsent()? none() : pyrowindex::wrap(ri);
}


PyObject* get_datatable_ptr(obj* self) {
  return PyLong_FromLongLong((long long int)self->ref);
}


/**
 * Return size of the referenced DataTable, but without the
 * `sizeof(pydatatable::obj)`, which includes the size of the `self->ref`
 * pointer.
 */
PyObject* get_alloc_size(obj* self) {
  return PyLong_FromSize_t(self->ref->memory_footprint());
}



//==============================================================================
// PyDatatable methods
//==============================================================================

PyObject* window(obj* self, PyObject* args) {
  int64_t row0, row1, col0, col1;
  if (!PyArg_ParseTuple(args, "llll", &row0, &row1, &col0, &col1))
    return nullptr;

  PyObject* nargs = Py_BuildValue("Ollll", self, row0, row1, col0, col1);
  PyObject* res = PyObject_CallObject((PyObject*) &pydatawindow::type, nargs);
  Py_XDECREF(nargs);

  return res;
}


PyObject* to_scalar(obj* self, PyObject*) {
  DataTable* dt = self->ref;
  if (dt->ncols == 1 && dt->nrows == 1) {
    Column* col = dt->columns[0];
    int64_t i = col->rowindex().nth(0);
    auto f = py_stype_formatters[col->stype()];
    return f(col, i);
  } else {
    throw ValueError() << ".scalar() method cannot be applied to a Frame with "
      "shape `[" << dt->nrows << " x " << dt->ncols << "]`";
  }
}


PyObject* check(obj* self, PyObject* args) {
  DataTable* dt = self->ref;
  PyObject* stream = nullptr;

  if (!PyArg_ParseTuple(args, "|O:check", &stream)) return nullptr;

  IntegrityCheckContext icc(200);
  dt->verify_integrity(icc);
  if (icc.has_errors()) {
    if (stream) {
      PyObject* ret = PyObject_CallMethod(stream, "write", "s",
                                          icc.errors().str().c_str());
      if (ret == nullptr) return nullptr;
      Py_DECREF(ret);
    }
    else {
      std::cout << icc.errors().str();
    }
  }
  return incref(icc.has_errors()? Py_False : Py_True);
}


PyObject* column(obj* self, PyObject* args) {
  DataTable* dt = self->ref;
  int64_t colidx;
  if (!PyArg_ParseTuple(args, "l:column", &colidx))
    return nullptr;
  if (colidx < -dt->ncols || colidx >= dt->ncols) {
    PyErr_Format(PyExc_ValueError, "Invalid column index %lld", colidx);
    return nullptr;
  }
  if (colidx < 0) colidx += dt->ncols;
  pycolumn::obj* pycol =
      pycolumn::from_column(dt->columns[colidx], self, colidx);
  return (PyObject*) pycol;
}



PyObject* delete_columns(obj* self, PyObject* args) {
  DataTable* dt = self->ref;
  PyObject* list;
  if (!PyArg_ParseTuple(args, "O!:delete_columns", &PyList_Type, &list))
    return nullptr;

  int ncols = (int) PyList_Size(list);
  int *cols_to_remove = nullptr;
  dtmalloc(cols_to_remove, int, ncols);
  for (int i = 0; i < ncols; i++) {
    PyObject* item = PyList_GET_ITEM(list, i);
    cols_to_remove[i] = (int) PyLong_AsLong(item);
  }
  dt->delete_columns(cols_to_remove, ncols);

  dtfree(cols_to_remove);
  return none();
}



PyObject* replace_rowindex(obj* self, PyObject* args) {
  DataTable* dt = self->ref;
  PyObject* arg1;
  if (!PyArg_ParseTuple(args, "O:replace_rowindex", &arg1))
    return nullptr;
  RowIndex newri = PyObj(arg1).as_rowindex();

  dt->replace_rowindex(newri);
  return none();
}



PyObject* rbind(obj* self, PyObject* args) {
  DataTable* dt = self->ref;
  int final_ncols;
  PyObject* list;
  if (!PyArg_ParseTuple(args, "iO!:delete_columns",
                        &final_ncols, &PyList_Type, &list))
    return nullptr;

  int ndts = (int) PyList_Size(list);
  DataTable** dts = nullptr;
  dtmalloc(dts, DataTable*, ndts);
  int** cols_to_append = nullptr;
  dtmalloc(cols_to_append, int*, final_ncols);
  for (int i = 0; i < final_ncols; i++) {
    dtmalloc(cols_to_append[i], int, ndts);
  }
  for (int i = 0; i < ndts; i++) {
    PyObject* item = PyList_GET_ITEM(list, i);
    DataTable* dti;
    PyObject* colslist;
    if (!PyArg_ParseTuple(item, "O&O",
                          &unwrap, &dti, &colslist))
      return nullptr;
    int64_t j = 0;
    if (colslist == Py_None) {
      int64_t ncolsi = dti->ncols;
      for (; j < ncolsi; ++j) {
        cols_to_append[j][i] = static_cast<int>(j);
      }
    } else {
      int64_t ncolsi = PyList_Size(colslist);
      for (; j < ncolsi; ++j) {
        PyObject* itemj = PyList_GET_ITEM(colslist, j);
        cols_to_append[j][i] = (itemj == Py_None)? -1
                               : (int) PyLong_AsLong(itemj);
      }
    }
    for (; j < final_ncols; ++j) {
      cols_to_append[j][i] = -1;
    }
    dts[i] = dti;
  }

  dt->rbind(dts, cols_to_append, ndts, final_ncols);

  dtfree(cols_to_append);
  dtfree(dts);
  return none();
}


PyObject* cbind(obj* self, PyObject* args) {
  PyObject* pydts;
  if (!PyArg_ParseTuple(args, "O!:cbind",
                        &PyList_Type, &pydts)) return nullptr;

  DataTable* dt = self->ref;
  int ndts = (int) PyList_Size(pydts);
  DataTable** dts = nullptr;
  dtmalloc(dts, DataTable*, ndts);
  for (int i = 0; i < ndts; i++) {
    PyObject* elem = PyList_GET_ITEM(pydts, i);
    if (!PyObject_TypeCheck(elem, &type)) {
      PyErr_Format(PyExc_ValueError,
          "Element %d of the array is not a DataTable object", i);
      return nullptr;
    }
    dts[i] = ((pydatatable::obj*) elem)->ref;
  }
  DataTable* ret = dt->cbind( dts, ndts);
  if (ret == nullptr) return nullptr;

  dtfree(dts);
  return none();
}



PyObject* sort(obj* self, PyObject* args) {
  DataTable* dt = self->ref;
  int idx = 0;
  int make_groups = 0;
  if (!PyArg_ParseTuple(args, "i|i:sort", &idx, &make_groups))
    return nullptr;

  arr32_t cols(1);
  cols[0] = idx;
  RowIndex ri = dt->sortby(cols, make_groups);
  return pyrowindex::wrap(ri);
}



PyObject* get_min    (obj* self, PyObject*) { return wrap(self->ref->min_datatable()); }
PyObject* get_max    (obj* self, PyObject*) { return wrap(self->ref->max_datatable()); }
PyObject* get_mode   (obj* self, PyObject*) { return wrap(self->ref->mode_datatable()); }
PyObject* get_mean   (obj* self, PyObject*) { return wrap(self->ref->mean_datatable()); }
PyObject* get_sd     (obj* self, PyObject*) { return wrap(self->ref->sd_datatable()); }
PyObject* get_sum    (obj* self, PyObject*) { return wrap(self->ref->sum_datatable()); }
PyObject* get_countna(obj* self, PyObject*) { return wrap(self->ref->countna_datatable()); }
PyObject* get_nunique(obj* self, PyObject*) { return wrap(self->ref->nunique_datatable()); }
PyObject* get_nmodal (obj* self, PyObject*) { return wrap(self->ref->nmodal_datatable()); }

typedef PyObject* (Column::*scalarstatfn)() const;
static PyObject* _scalar_stat(DataTable* dt, scalarstatfn f) {
  if (dt->ncols != 1) throw ValueError() << "This method can only be applied to a 1-column Frame";
  return (dt->columns[0]->*f)();
}

PyObject* countna1(obj* self, PyObject*) { return _scalar_stat(self->ref, &Column::countna_pyscalar); }
PyObject* nunique1(obj* self, PyObject*) { return _scalar_stat(self->ref, &Column::nunique_pyscalar); }
PyObject* nmodal1 (obj* self, PyObject*) { return _scalar_stat(self->ref, &Column::nmodal_pyscalar); }
PyObject* min1    (obj* self, PyObject*) { return _scalar_stat(self->ref, &Column::min_pyscalar); }
PyObject* max1    (obj* self, PyObject*) { return _scalar_stat(self->ref, &Column::max_pyscalar); }
PyObject* mode1   (obj* self, PyObject*) { return _scalar_stat(self->ref, &Column::mode_pyscalar); }
PyObject* mean1   (obj* self, PyObject*) { return _scalar_stat(self->ref, &Column::mean_pyscalar); }
PyObject* sd1     (obj* self, PyObject*) { return _scalar_stat(self->ref, &Column::sd_pyscalar); }
PyObject* sum1    (obj* self, PyObject*) { return _scalar_stat(self->ref, &Column::sum_pyscalar); }



PyObject* materialize(obj* self, PyObject*) {
  DataTable* dt = self->ref;
  if (dt->rowindex.isabsent()) {
    PyErr_Format(PyExc_ValueError, "Only a view can be materialized");
    return nullptr;
  }

  Column** cols = nullptr;
  dtmalloc(cols, Column*, dt->ncols + 1);
  for (int64_t i = 0; i < dt->ncols; ++i) {
    cols[i] = dt->columns[i]->shallowcopy();
    if (cols[i] == nullptr) return nullptr;
    cols[i]->reify();
  }
  cols[dt->ncols] = nullptr;

  DataTable* newdt = new DataTable(cols);
  return wrap(newdt);
}


PyObject* apply_na_mask(obj* self, PyObject* args) {
  DataTable* dt = self->ref;
  DataTable* mask = nullptr;
  if (!PyArg_ParseTuple(args, "O&", &unwrap, &mask)) return nullptr;

  dt->apply_na_mask(mask);
  return none();
}


PyObject* use_stype_for_buffers(obj* self, PyObject* args) {
  int st = 0;
  if (!PyArg_ParseTuple(args, "|i:use_stype_for_buffers", &st))
    return nullptr;
  self->use_stype_for_buffers = static_cast<SType>(st);
  return none();
}


static void dealloc(obj* self) {
  delete self->ref;
  Py_TYPE(self)->tp_free((PyObject*)self);
}



//==============================================================================
// DataTable type definition
//==============================================================================

static PyMethodDef datatable_methods[] = {
  METHODv(window),
  METHOD0(to_scalar),
  METHODv(check),
  METHODv(column),
  METHODv(delete_columns),
  METHODv(replace_rowindex),
  METHODv(rbind),
  METHODv(cbind),
  METHODv(sort),
  METHOD0(get_min),
  METHOD0(get_max),
  METHOD0(get_mode),
  METHOD0(get_sum),
  METHOD0(get_mean),
  METHOD0(get_sd),
  METHOD0(get_countna),
  METHOD0(get_nunique),
  METHOD0(get_nmodal),
  METHOD0(countna1),
  METHOD0(nunique1),
  METHOD0(nmodal1),
  METHOD0(min1),
  METHOD0(max1),
  METHOD0(mode1),
  METHOD0(sum1),
  METHOD0(mean1),
  METHOD0(sd1),
  METHOD0(materialize),
  METHODv(apply_na_mask),
  METHODv(use_stype_for_buffers),
  {nullptr, nullptr, 0, nullptr}           /* sentinel */
};

static PyGetSetDef datatable_getseters[] = {
  GETTER(nrows),
  GETTER(ncols),
  GETTER(ltypes),
  GETTER(stypes),
  GETTER(isview),
  GETTER(rowindex),
  GETTER(rowindex_type),
  GETTER(datatable_ptr),
  GETTER(alloc_size),
  {nullptr, nullptr, nullptr, nullptr, nullptr}  /* sentinel */
};

PyTypeObject type = {
  PyVarObject_HEAD_INIT(nullptr, 0)
  cls_name,                           /* tp_name */
  sizeof(pydatatable::obj),           /* tp_basicsize */
  0,                                  /* tp_itemsize */
  DESTRUCTOR,                         /* tp_dealloc */
  0,                                  /* tp_print */
  0,                                  /* tp_getattr */
  0,                                  /* tp_setattr */
  0,                                  /* tp_compare */
  0,                                  /* tp_repr */
  0,                                  /* tp_as_number */
  0,                                  /* tp_as_sequence */
  0,                                  /* tp_as_mapping */
  0,                                  /* tp_hash  */
  0,                                  /* tp_call */
  0,                                  /* tp_str */
  0,                                  /* tp_getattro */
  0,                                  /* tp_setattro */
  &pydatatable::as_buffer,            /* tp_as_buffer;  see py_buffers.cc */
  Py_TPFLAGS_DEFAULT,                 /* tp_flags */
  cls_doc,                            /* tp_doc */
  0,                                  /* tp_traverse */
  0,                                  /* tp_clear */
  0,                                  /* tp_richcompare */
  0,                                  /* tp_weaklistoffset */
  0,                                  /* tp_iter */
  0,                                  /* tp_iternext */
  datatable_methods,                  /* tp_methods */
  0,                                  /* tp_members */
  datatable_getseters,                /* tp_getset */
  0,                                  /* tp_base */
  0,                                  /* tp_dict */
  0,                                  /* tp_descr_get */
  0,                                  /* tp_descr_set */
  0,                                  /* tp_dictoffset */
  0,                                  /* tp_init */
  0,                                  /* tp_alloc */
  0,                                  /* tp_new */
  0,                                  /* tp_free */
  0,                                  /* tp_is_gc */
  0,                                  /* tp_bases */
  0,                                  /* tp_mro */
  0,                                  /* tp_cache */
  0,                                  /* tp_subclasses */
  0,                                  /* tp_weaklist */
  0,                                  /* tp_del */
  0,                                  /* tp_version_tag */
  0,                                  /* tp_finalize */
};


int static_init(PyObject* module) {
  // Register type on the module
  pydatatable::type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&pydatatable::type) < 0) return 0;
  PyObject* typeobj = reinterpret_cast<PyObject*>(&type);
  Py_INCREF(typeobj);
  PyModule_AddObject(module, "DataTable", typeobj);

  strRowIndexTypeArr32 = PyUnicode_FromString("arr32");
  strRowIndexTypeArr64 = PyUnicode_FromString("arr64");
  strRowIndexTypeSlice = PyUnicode_FromString("slice");
  return 1;
}


};  // namespace pydatatable
