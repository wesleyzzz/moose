//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

#include "AddPeriodicBCAction.h"

// MOOSE includes
#include "DisplacedProblem.h"
#include "FEProblem.h"
#include "FunctionPeriodicBoundary.h"
#include "GeneratedMesh.h"
#include "InputParameters.h"
#include "MooseMesh.h"
#include "MooseVariableFE.h"
#include "NonlinearSystem.h"
#include "RelationshipManager.h"

#include "libmesh/periodic_boundary.h" // translation PBCs provided by libmesh

registerMooseAction("MooseApp", AddPeriodicBCAction, "add_periodic_bc");
registerMooseAction("MooseApp", AddPeriodicBCAction, "add_geometric_rm");

template <>
InputParameters
validParams<AddPeriodicBCAction>()
{
  InputParameters params = validParams<Action>();
  params.addParam<std::vector<std::string>>("auto_direction",
                                            "If using a generated mesh, you can "
                                            "specifiy just the dimension(s) you "
                                            "want to mark as periodic");

  params.addParam<BoundaryName>("primary", "Boundary ID associated with the primary boundary.");
  params.addParam<BoundaryName>("secondary", "Boundary ID associated with the secondary boundary.");
  params.addParam<RealVectorValue>("translation",
                                   "Vector that translates coordinates on the "
                                   "primary boundary to coordinates on the "
                                   "secondary boundary.");
  params.addParam<std::vector<std::string>>("transform_func",
                                            "Functions that specify the transformation");
  params.addParam<std::vector<std::string>>("inv_transform_func",
                                            "Functions that specify the inverse transformation");

  params.addParam<std::vector<VariableName>>("variable", "Variable for the periodic boundary");
  return params;
}

AddPeriodicBCAction::AddPeriodicBCAction(InputParameters params) : Action(params) {}

void
AddPeriodicBCAction::setPeriodicVars(PeriodicBoundaryBase & p,
                                     const std::vector<VariableName> & var_names)
{
  NonlinearSystemBase & nl = _problem->getNonlinearSystemBase();
  const std::vector<VariableName> * var_names_ptr;

  // If var_names is empty - then apply this periodic condition to all variables in the system
  if (var_names.empty())
    var_names_ptr = &nl.getVariableNames();
  else
    var_names_ptr = &var_names;

  for (const auto & var_name : *var_names_ptr)
  {
    if (!nl.hasScalarVariable(var_name))
    {
      unsigned int var_num = nl.getVariable(0, var_name).number();
      p.set_variable(var_num);
      _mesh->addPeriodicVariable(var_num, p.myboundary, p.pairedboundary);
    }
  }
}

bool
AddPeriodicBCAction::autoTranslationBoundaries()
{
  auto displaced_problem = _problem->getDisplacedProblem();

  if (isParamValid("auto_direction"))
  {
    // If we are working with a parallel mesh then we're going to ghost all the boundaries
    // everywhere because we don't know what we need...
    if (_mesh->isDistributedMesh())
    {
      const std::set<BoundaryID> & ids = _mesh->meshBoundaryIds();
      for (const auto & bid : ids)
        _problem->addGhostedBoundary(bid);

      _problem->ghostGhostedBoundaries();

      bool is_orthogonal_mesh = _mesh->detectOrthogonalDimRanges();

      // If we can't detect the orthogonal dimension ranges for this
      // Mesh, then auto_direction periodicity isn't going to work.
      if (!is_orthogonal_mesh)
        mooseError("Could not detect orthogonal dimension ranges for DistributedMesh.");
    }

    NonlinearSystemBase & nl = _problem->getNonlinearSystemBase();
    std::vector<std::string> auto_dirs = getParam<std::vector<std::string>>("auto_direction");

    int dim_offset = _mesh->dimension() - 2;
    for (const auto & dir : auto_dirs)
    {
      int component = -1;
      if (dir == "X" || dir == "x")
        component = 0;
      else if (dir == "Y" || dir == "y")
      {
        if (dim_offset < 0)
          mooseError("Cannot wrap 'Y' direction when using a 1D mesh");
        component = 1;
      }
      else if (dir == "Z" || dir == "z")
      {
        if (dim_offset <= 0)
          mooseError("Cannot wrap 'Z' direction when using a 1D or 2D mesh");
        component = 2;
      }

      if (component >= 0)
      {
        const std::pair<BoundaryID, BoundaryID> * boundary_ids =
            _mesh->getPairedBoundaryMapping(component);
        RealVectorValue v;
        v(component) = _mesh->dimensionWidth(component);
        PeriodicBoundary p(v);

        if (boundary_ids == NULL)
          mooseError(
              "Couldn't auto-detect a paired boundary for use with periodic boundary conditions");

        p.myboundary = boundary_ids->first;
        p.pairedboundary = boundary_ids->second;
        setPeriodicVars(p, getParam<std::vector<VariableName>>("variable"));
        nl.dofMap().add_periodic_boundary(p);
        if (displaced_problem)
          displaced_problem->nlSys().dofMap().add_periodic_boundary(p);
      }
    }
    return true;
  }
  return false;
}

void
AddPeriodicBCAction::act()
{
  if (_current_task == "add_geometric_rm")
  {
    auto rm_params = _factory.getValidParams("ElementSideNeighborLayers");

    rm_params.set<std::string>("for_whom") = "PeriodicBCs";
    rm_params.set<MooseMesh *>("mesh") = Action::_mesh.get();
    rm_params.set<Moose::RelationshipManagerType>("rm_type") =
        Moose::RelationshipManagerType::GEOMETRIC | Moose::RelationshipManagerType::ALGEBRAIC;

    // We can't attach this relationship manager early because
    // the PeriodicBoundaries object needs to exist!
    rm_params.set<bool>("attach_geometric_early") = false;

    if (rm_params.areAllRequiredParamsValid())
    {
      auto rm_obj = _factory.create<RelationshipManager>(
          "ElementSideNeighborLayers", "periodic_bc_ghosting_" + name(), rm_params);

      if (!_app.addRelationshipManager(rm_obj))
        _factory.releaseSharedObjects(*rm_obj);
    }
    else
      mooseError("Invalid initialization of ElementSideNeighborLayers");
  }

  if (_current_task == "add_periodic_bc")
  {
    NonlinearSystemBase & nl = _problem->getNonlinearSystemBase();
    _mesh = &_problem->mesh();
    auto displaced_problem = _problem->getDisplacedProblem();

    if (autoTranslationBoundaries())
      return;

    if (_pars.isParamValid("translation"))
    {
      RealVectorValue translation = getParam<RealVectorValue>("translation");

      PeriodicBoundary p(translation);
      p.myboundary = _mesh->getBoundaryID(getParam<BoundaryName>("primary"));
      p.pairedboundary = _mesh->getBoundaryID(getParam<BoundaryName>("secondary"));
      setPeriodicVars(p, getParam<std::vector<VariableName>>("variable"));

      _problem->addGhostedBoundary(p.myboundary);
      _problem->addGhostedBoundary(p.pairedboundary);

      nl.dofMap().add_periodic_boundary(p);
      if (displaced_problem)
        displaced_problem->nlSys().dofMap().add_periodic_boundary(p);
    }
    else if (getParam<std::vector<std::string>>("transform_func") != std::vector<std::string>())
    {
      std::vector<std::string> inv_fn_names =
          getParam<std::vector<std::string>>("inv_transform_func");
      std::vector<std::string> fn_names = getParam<std::vector<std::string>>("transform_func");

      // If the user provided a forward transformation, he must also provide an inverse -- we can't
      // form the inverse of an arbitrary function automatically...
      if (inv_fn_names == std::vector<std::string>())
        mooseError("You must provide an inv_transform_func for FunctionPeriodicBoundary!");

      FunctionPeriodicBoundary pb(*_problem, fn_names);
      pb.myboundary = _mesh->getBoundaryID(getParam<BoundaryName>("primary"));
      pb.pairedboundary = _mesh->getBoundaryID(getParam<BoundaryName>("secondary"));
      setPeriodicVars(pb, getParam<std::vector<VariableName>>("variable"));

      FunctionPeriodicBoundary ipb(*_problem, inv_fn_names);
      ipb.myboundary =
          _mesh->getBoundaryID(getParam<BoundaryName>("secondary")); // these are swapped
      ipb.pairedboundary =
          _mesh->getBoundaryID(getParam<BoundaryName>("primary")); // these are swapped
      setPeriodicVars(ipb, getParam<std::vector<VariableName>>("variable"));

      _problem->addGhostedBoundary(ipb.myboundary);
      _problem->addGhostedBoundary(ipb.pairedboundary);

      // Add the pair of periodic boundaries to the dof map
      nl.dofMap().add_periodic_boundary(pb, ipb);
      if (displaced_problem)
        displaced_problem->nlSys().dofMap().add_periodic_boundary(pb, ipb);
    }
    else
    {
      mooseError(
          "You have to specify either 'auto_direction', 'translation' or 'trans_func' in your "
          "period boundary section '" +
          _name + "'");
    }
  }
}
