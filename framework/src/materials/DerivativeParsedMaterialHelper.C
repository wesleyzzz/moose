//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

#include "DerivativeParsedMaterialHelper.h"
#include "Conversion.h"

#include <deque>

#include "libmesh/quadrature.h"

template <>
InputParameters
validParams<DerivativeParsedMaterialHelper>()
{
  InputParameters params = validParams<ParsedMaterialHelper>();
  params.addClassDescription("Parsed Function Material with automatic derivatives.");
  params.addDeprecatedParam<bool>("third_derivatives",
                                  "Flag to indicate if third derivatives are needed",
                                  "Use derivative_order instead.");
  params.addParam<unsigned int>("derivative_order", 3, "Maximum order of derivatives taken");

  return params;
}

DerivativeParsedMaterialHelper::DerivativeParsedMaterialHelper(const InputParameters & parameters,
                                                               VariableNameMappingMode map_mode)
  : ParsedMaterialHelper(parameters, map_mode),
    //_derivative_order(getParam<unsigned int>("derivative_order"))
    _dmatvar_base("matpropautoderiv"),
    _dmatvar_index(0),
    _derivative_order(isParamValid("third_derivatives")
                          ? (getParam<bool>("third_derivatives") ? 3 : 2)
                          : getParam<unsigned int>("derivative_order"))
{
}

void
DerivativeParsedMaterialHelper::functionsPostParse()
{
  // optimize base function
  ParsedMaterialHelper::functionsOptimize();

  // generate derivatives
  assembleDerivatives();

  // force a value update to get the property at least once and register it for the dependencies
  for (auto & mpd : _mat_prop_descriptors)
    mpd.value();
}

ParsedMaterialHelper::MatPropDescriptorList::iterator
DerivativeParsedMaterialHelper::findMatPropDerivative(const FunctionMaterialPropertyDescriptor & m)
{
  std::string name = m.getPropertyName();
  for (MatPropDescriptorList::iterator i = _mat_prop_descriptors.begin();
       i != _mat_prop_descriptors.end();
       ++i)
    if (i->getPropertyName() == name)
      return i;

  return _mat_prop_descriptors.end();
}

/**
 * Perform a breadth first construction of all requested derivatives.
 */
void
DerivativeParsedMaterialHelper::assembleDerivatives()
{
  // need to check for zero derivatives here, otherwise at least one order is generated
  if (_derivative_order < 1)
    return;

  // if we are not on thread 0 we fetch all data from the thread 0 copy that already did all the
  // work
  if (_tid > 0)
  {
    // get the master object from thread 0
    const MaterialWarehouse & material_warehouse = _fe_problem.getMaterialWarehouse();
    const MooseObjectWarehouse<Material> & warehouse = material_warehouse[_material_data_type];

    MooseSharedPointer<DerivativeParsedMaterialHelper> master =
        MooseSharedNamespace::dynamic_pointer_cast<DerivativeParsedMaterialHelper>(
            warehouse.getActiveObject(name()));

    // copy parsers and declare properties
    for (MooseIndex(master->_derivatives) i = 0; i < master->_derivatives.size(); ++i)
    {
      Derivative newderivative;
      newderivative.first =
          &declarePropertyDerivative<Real>(_F_name, master->_derivatives[i].darg_names);
      newderivative.second = ADFunctionPtr(new ADFunction(*master->_derivatives[i].second));
      _derivatives.push_back(newderivative);
    }

    // copy coupled material properties
    auto start = _mat_prop_descriptors.size();
    for (MooseIndex(master->_mat_prop_descriptors) i = start;
         i < master->_mat_prop_descriptors.size();
         ++i)
    {
      FunctionMaterialPropertyDescriptor newdescriptor(master->_mat_prop_descriptors[i], this);
      _mat_prop_descriptors.push_back(newdescriptor);
    }

    // size parameter buffer
    _func_params.resize(master->_func_params.size());
    return;
  }

  // set up job queue. We need a deque here to be able to iterate over the currently queued items.
  std::deque<QueueItem> queue;
  queue.push_back(QueueItem(_func_F));

  // generate derivatives until the queue is exhausted
  while (!queue.empty())
  {
    QueueItem current = queue.front();

    // Add necessary derivative steps. All permutations of one set of derivatives are equal, so we
    // make sure to generate only one each.
    for (auto i = current._dargs.empty() ? 0u : current._dargs.back(); i < _nargs; ++i)
    {
      // go through list of material properties and check if derivatives are needed
      auto ndesc = _mat_prop_descriptors.size();
      for (MooseIndex(_mat_prop_descriptors) jj = 0; jj < ndesc; ++jj)
      {
        FunctionMaterialPropertyDescriptor * j = &_mat_prop_descriptors[jj];

        // take a property descriptor and check if it depends on the current derivative variable
        if (j->dependsOn(_arg_names[i]))
        {
          FunctionMaterialPropertyDescriptor matderivative(*j);
          matderivative.addDerivative(_arg_names[i]);

          // search if this new derivative is not yet in the list of material properties
          MatPropDescriptorList::iterator m = findMatPropDerivative(matderivative);
          if (m == _mat_prop_descriptors.end())
          {
            // construct new variable name for the material property derivative as base name +
            // number
            std::string newvarname = _dmatvar_base + Moose::stringify(_dmatvar_index++);
            matderivative.setSymbolName(newvarname);

            // loop over all queue items to register the new dmatvar variable (includes 'current'
            // which is popped below)
            for (std::deque<QueueItem>::iterator k = queue.begin(); k != queue.end(); ++k)
            {
              k->_F->AddVariable(newvarname);
              k->_F->RegisterDerivative(j->getSymbolName(), _arg_names[i], newvarname);
            }

            _mat_prop_descriptors.push_back(matderivative);
          }
        }
      }

      // construct new derivative
      QueueItem newitem = current;
      newitem._dargs.push_back(i);

      // build derivative
      newitem._F = ADFunctionPtr(new ADFunction(*current._F));
      if (newitem._F->AutoDiff(_variable_names[i]) != -1)
        mooseError(
            "Failed to take order ", newitem._dargs.size(), " derivative in material ", _name);

      // optimize and compile
      if (!_disable_fpoptimizer)
        newitem._F->Optimize();
      if (_enable_jit && !newitem._F->JITCompile())
        mooseInfo("Failed to JIT compile expression, falling back to byte code interpretation.");

      // generate material property argument vector
      std::vector<VariableName> darg_names(0);
      for (unsigned int j = 0; j < newitem._dargs.size(); ++j)
        darg_names.push_back(_arg_names[newitem._dargs[j]]);

      // append to list of derivatives if the derivative is non-vanishing
      if (!newitem._F->isZero())
      {
        Derivative newderivative;
        newderivative.first = &declarePropertyDerivative<Real>(_F_name, darg_names);
        newderivative.second = newitem._F;
        newderivative.darg_names = darg_names;
        _derivatives.push_back(newderivative);
      }

      // push item to queue if further differentiation is required
      if (newitem._dargs.size() < _derivative_order)
        queue.push_back(newitem);
    }

    // remove the 'current' element from the queue
    queue.pop_front();
  }

  // increase the parameter buffer to provide storage for the material property derivatives
  _func_params.resize(_nargs + _mat_prop_descriptors.size());
}

void
DerivativeParsedMaterialHelper::initQpStatefulProperties()
{
  if (_prop_F)
    (*_prop_F)[_qp] = 0.0;

  for (auto & D : _derivatives)
    (*D.first)[_qp] = 0.0;
}

void
DerivativeParsedMaterialHelper::computeQpProperties()
{
  // fill the parameter vector, apply tolerances
  for (unsigned int i = 0; i < _nargs; ++i)
  {
    if (_tol[i] < 0.0)
      _func_params[i] = (*_args[i])[_qp];
    else
    {
      Real a = (*_args[i])[_qp];
      _func_params[i] = a < _tol[i] ? _tol[i] : (a > 1.0 - _tol[i] ? 1.0 - _tol[i] : a);
    }
  }

  // insert material property values
  auto nmat_props = _mat_prop_descriptors.size();
  for (MooseIndex(_mat_prop_descriptors) i = 0; i < nmat_props; ++i)
    _func_params[i + _nargs] = _mat_prop_descriptors[i].value()[_qp];

  // set function value
  if (_prop_F)
    (*_prop_F)[_qp] = evaluate(_func_F);

  // set derivatives
  for (auto & D : _derivatives)
    (*D.first)[_qp] = evaluate(D.second);
}
