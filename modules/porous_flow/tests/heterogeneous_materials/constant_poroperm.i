# Assign porosity and permeability variables from constant AuxVariables to create
# a heterogeneous model

[Mesh]
  type = GeneratedMesh
  dim = 3
  nx = 3
  ny = 3
  nz = 3
  xmax = 3
  ymax = 3
  zmax = 3
[]

[GlobalParams]
  PorousFlowDictator = dictator
  gravity = '0 0 -10'
[]

[Variables]
  [./ppwater]
    initial_condition = 1.5e6
  [../]
[]

[AuxVariables]
  [./poro]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permxx]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permxy]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permxz]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permyx]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permyy]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permyz]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permzx]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permzy]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permzz]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./poromat]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permxxmat]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permxymat]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permxzmat]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permyxmat]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permyymat]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permyzmat]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permzxmat]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permzymat]
    family = MONOMIAL
    order = CONSTANT
  [../]
  [./permzzmat]
    family = MONOMIAL
    order = CONSTANT
  [../]
[]

[AuxKernels]
  [./poromat]
    type = MaterialRealAux
    property = PorousFlow_porosity_qp
    variable = poromat
  [../]
  [./permxxmat]
    type = MaterialRealTensorValueAux
    property = PorousFlow_permeability_qp
    variable = permxxmat
    column = 0
    row = 0
  [../]
  [./permxymat]
    type = MaterialRealTensorValueAux
    property = PorousFlow_permeability_qp
    variable = permxymat
    column = 1
    row = 0
  [../]
  [./permxzmat]
    type = MaterialRealTensorValueAux
    property = PorousFlow_permeability_qp
    variable = permxzmat
    column = 2
    row = 0
  [../]
  [./permyxmat]
    type = MaterialRealTensorValueAux
    property = PorousFlow_permeability_qp
    variable = permyxmat
    column = 0
    row = 1
  [../]
  [./permyymat]
    type = MaterialRealTensorValueAux
    property = PorousFlow_permeability_qp
    variable = permyymat
    column = 1
    row = 1
  [../]
  [./permyzmat]
    type = MaterialRealTensorValueAux
    property = PorousFlow_permeability_qp
    variable = permyzmat
    column = 2
    row = 1
  [../]
  [./permzxmat]
    type = MaterialRealTensorValueAux
    property = PorousFlow_permeability_qp
    variable = permzxmat
    column = 0
    row = 2
  [../]
  [./permzymat]
    type = MaterialRealTensorValueAux
    property = PorousFlow_permeability_qp
    variable = permzymat
    column = 1
    row = 2
  [../]
  [./permzzmat]
    type = MaterialRealTensorValueAux
    property = PorousFlow_permeability_qp
    variable = permzzmat
    column = 2
    row = 2
  [../]
[]

[ICs]
  [./poro]
    type = RandomIC
    seed = 0
    variable = poro
    max = 0.5
    min = 0.1
  [../]
  [./permx]
    type = FunctionIC
    function = permx
    variable = permxx
  [../]
  [./permy]
    type = FunctionIC
    function = permy
    variable = permyy
  [../]
  [./permz]
    type = FunctionIC
    function = permz
    variable = permzz
  [../]
[]

[Functions]
  [./permx]
    type = ParsedFunction
    value = '(1+x)*1e-11'
  [../]
  [./permy]
    type = ParsedFunction
    value = '(1+y)*1e-11'
  [../]
  [./permz]
    type = ParsedFunction
    value = '(1+z)*1e-11'
  [../]
[]

[Kernels]
  [./mass0]
    type = PorousFlowMassTimeDerivative
    variable = ppwater
  [../]
  [./flux0]
    type = PorousFlowAdvectiveFlux
    variable = ppwater
  [../]
[]

[UserObjects]
  [./dictator]
    type = PorousFlowDictator
    porous_flow_vars = 'ppwater'
    number_fluid_phases = 1
    number_fluid_components = 1
  [../]
[]

[Modules]
  [./FluidProperties]
    [./simple_fluid]
      type = SimpleFluidProperties
      bulk_modulus = 2e9
      density0 = 1000
      viscosity = 1e-3
      thermal_expansion = 0
      cv = 2
    [../]
  [../]
[]

[Materials]
  [./temperature]
    type = PorousFlowTemperature
  [../]
  [./temperature_nodal]
    type = PorousFlowTemperature
    at_nodes = true
  [../]
  [./ppss]
    type = PorousFlow1PhaseFullySaturated
    porepressure = ppwater
  [../]
  [./ppss_nodal]
    type = PorousFlow1PhaseFullySaturated
    at_nodes = true
    porepressure = ppwater
  [../]
  [./massfrac]
    type = PorousFlowMassFraction
    at_nodes = true
  [../]
  [./simple_fluid]
    type = PorousFlowSingleComponentFluid
    fp = simple_fluid
    phase = 0
    at_nodes = true
  [../]
  [./simple_fluid_qp]
    type = PorousFlowSingleComponentFluid
    fp = simple_fluid
    phase = 0
  [../]
  [./dens_all]
    type = PorousFlowJoiner
    include_old = true
    material_property = PorousFlow_fluid_phase_density_nodal
  [../]
  [./dens_qp_all]
    type = PorousFlowJoiner
    material_property = PorousFlow_fluid_phase_density_qp
    at_nodes = false
  [../]
  [./visc_all]
    type = PorousFlowJoiner
    at_nodes = true
    material_property = PorousFlow_viscosity_nodal
  [../]
  [./porosity]
    type = PorousFlowPorosityConst
    porosity = poro
  [../]
  [./porosity_nodal]
    type = PorousFlowPorosityConst
    at_nodes = true
    porosity = poro
  [../]
  [./permeability]
    type = PorousFlowPermeabilityConstFromVar
    perm_xx = permxx
    perm_yy = permyy
    perm_zz = permzz
  [../]
  [./relperm_water]
    type = PorousFlowRelativePermeabilityCorey
    at_nodes = true
    n = 2
    phase = 0
  [../]
  [./relperm_all]
    type = PorousFlowJoiner
    at_nodes = true
    material_property = PorousFlow_relative_permeability_nodal
  [../]
[]

[Postprocessors]
  [./mass_ph0]
    type = PorousFlowFluidMass
    fluid_component = 0
    execute_on = 'initial timestep_end'
  [../]
[]

[Preconditioning]
  [./smp]
    type = SMP
    full = true
    petsc_options_iname = '-ksp_type -pc_type -snes_atol -snes_rtol'
    petsc_options_value = 'bcgs bjacobi 1E-12 1E-10'
  [../]
[]

[Executioner]
  type = Transient
  solve_type = Newton
  end_time = 100
  dt = 100
[]

[Outputs]
  execute_on = 'initial timestep_end'
  exodus = true
  print_perf_log = true
[]
