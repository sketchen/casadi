#include <casadi/stl_vector_tools.hpp>
#include <interfaces/ipopt/ipopt_solver.hpp>
#include <interfaces/sundials/idas_integrator.hpp>
#include <interfaces/sundials/cvodes_integrator.hpp>
#include <interfaces/sundials/kinsol_solver.hpp>

#include <casadi/fx/fx_tools.hpp>
#include <casadi/mx/mx_tools.hpp>
#include <casadi/sx/sx_tools.hpp>
#include <casadi/matrix/matrix_tools.hpp>
#include <casadi/fx/jacobian.hpp>

#include <optimal_control/fmi_parser.hpp>
#include <optimal_control/ocp_tools.hpp>
#include <optimal_control/variable_tools.hpp>
#include <optimal_control/multiple_shooting.hpp>
#include <optimal_control/multiple_shooting_internal.hpp>

using namespace CasADi;
using namespace CasADi::Sundials;
using namespace CasADi::OptimalControl;
using namespace std;

double Tc_ref = 280;
double c_ref = 338.775766;
double T_ref = 280.099198;

double c_init = 956.271065;
double T_init = 250.051971;

int main(){

  // Allocate a parser and load the xml
  FMIParser parser("../examples/python/cstr/modelDescription.xml");

  // Dump representation to screen
  cout << "XML representation:" << endl;
  parser.print();

  // Obtain the symbolic representation of the OCP
  OCP ocp = parser.parse();

  // Print the ocp to screen
  ocp.print();

  // Scale the OCP
  OCP scaled_ocp = ocp.scale();
  
  // Sort the variables according to type
  OCPVariables var(ocp.variables);
  
  // Variables
  SX t = var.t.sx();
  Matrix<SX> x = sx(var.x);
  Matrix<SX> xdot = der(var.x);
  Matrix<SX> z = sx(var.z);
  Matrix<SX> p = sx(var.p);
  Matrix<SX> u = sx(var.u);

  Matrix<double> x_sca = nominal(var.x);
  Matrix<double> z_sca = nominal(var.z);
  Matrix<double> u_sca = nominal(var.u);
  Matrix<double> p_sca = nominal(var.p);

  cout <<  "x_sca = " << x_sca << endl;
  cout << "z_sca = " <<  z_sca << endl;
  cout << "u_sca = " << u_sca << endl;
  cout << "p_sca = " << p_sca << endl;

  // Initial guess for the state
  double x0d[] = {0.0, T_init, c_init};
  DMatrix x0 = vector<double>(x0d,x0d+3)/x_sca;

  // Initial guess for the control
  double u0d[] = {280};
  DMatrix u0 = vector<double>(u0d,u0d+1)/u_sca;
  
  // Create an implicit function
  vector<Matrix<SX> > impres_in(ODE_NUM_IN+1);
  impres_in[0] = xdot;
  impres_in[1+ODE_T] = t;
  impres_in[1+ODE_Y] = x;
  impres_in[1+ODE_P] = u;
  SXFunction impres(impres_in,SXMatrix(scaled_ocp.dae));
  KinsolSolver ode(impres);
  
/*  
  ode.init();
  
  ode.setInput(0.0,ODE_T);
  ode.setInput(x0,ODE_Y);
  ode.setInput(1.0,ODE_P);

  // set forward seed
  double x0_seed[] = {0, 1, 0};
  ode.setFwdSeed(x0_seed,ODE_Y);
  
  // set adjoint seed
  double xf_seed[] = {0, 1, 0};
  ode.setAdjSeed(xf_seed);
  
  ode.evaluate(1,1);
  cout << ode.adjSens(ODE_Y) << endl;
  cout << ode.fwdSens() << endl;
  
  DMatrix v = ode.output();

  DMatrix x0_pert = x0;
  x0_pert[2] += 0.0001;
  ode.setInput(x0_pert,ODE_Y);

  ode.evaluate();
  DMatrix v_pert = ode.output();
  
  cout << v << endl;
  cout << (v_pert-v)/0.0001 << endl;
  
  KinsolSolver Jode = ode.jac(ODE_Y);
  Jode.init();
  
  Jode.setInput(0.0,ODE_T);
  Jode.setInput(x0,ODE_Y);
  Jode.setInput(1.0,ODE_P);
  
  Jode.evaluate();
  
  cout << Jode.output() << endl;*/
    
  // Create an integrator
  vector<Matrix<SX> > dae_in(DAE_NUM_IN);
  dae_in[DAE_T] = t;
  dae_in[DAE_Y] = x;
  dae_in[DAE_YDOT] = xdot;
  dae_in[DAE_Z] = z;
  dae_in[DAE_P] = u;

  double dae_scad[] = {1e7,1000.,350.};
  vector<double> dae_sca(dae_scad,dae_scad+3);
  SXFunction dae(dae_in,SXMatrix(scaled_ocp.dae)/dae_sca);
  
  // Number of shooting nodes
  int num_nodes = 100;

  // Bounds on states
  vector<double> cfcn_lb;
  for(vector<SX>::iterator it=ocp.cfcn_lb.begin(); it!=ocp.cfcn_lb.end(); ++it)
    cfcn_lb.push_back(it->getValue());

  vector<double> cfcn_ub;
  for(vector<SX>::iterator it=ocp.cfcn_ub.begin(); it!=ocp.cfcn_ub.end(); ++it)
    cfcn_ub.push_back(it->getValue());
  
  IdasIntegrator integrator(dae);
  integrator.setOption("number_of_fwd_dir",1);
  integrator.setOption("number_of_adj_dir",0);
  integrator.setOption("fsens_err_con",true);
  integrator.setOption("quad_err_con",true);
  integrator.setOption("abstol",1e-8);
  integrator.setOption("reltol",1e-8);
  integrator.init();

  ode.init();
  CVodesIntegrator integrator2(ode);
  integrator2.setOption("number_of_fwd_dir",1);
  integrator2.setOption("number_of_adj_dir",0);
  integrator2.setOption("fsens_err_con",true);
  integrator2.setOption("quad_err_con",true);
  integrator2.setOption("abstol",1e-8);
  integrator2.setOption("reltol",1e-8);
  integrator2.init();

  // Number of differential states
  int nx = 3;

  // Number of controls
  int nu = 1;

  // Mayer objective function
  Matrix<SX> xf = symbolic("xf",nx,1);
  SXFunction mterm(xf, xf[0]);
  
  // Create a multiple shooting discretization
  MultipleShooting ms(integrator2,mterm);
  ms.setOption("number_of_grid_points",num_nodes);
  ms.setOption("final_time",ocp.tf);

  ms.init();

  for(int k=0; k<num_nodes; ++k){
    ms.input(OCP_U_INIT)[k] = 280/u_sca[0];
    ms.input(OCP_LBU)[k] = 230/u_sca[0];
    ms.input(OCP_UBU)[k] = 370/u_sca[0];
  }

  for(int k=0; k<=num_nodes; ++k){
    for(int i=0; i<nx; ++i){
      ms.input(OCP_X_INIT)[i+k*nx] = x0[i];
    }
  }
  
  // Bounds on state
  double inf = numeric_limits<double>::infinity();
  fill(ms.input(OCP_LBX).begin(),ms.input(OCP_LBX).end(),-inf);
  fill(ms.input(OCP_UBX).begin(),ms.input(OCP_UBX).end(),inf);
  for(int k=1; k<=num_nodes; ++k)
    ms.input(OCP_UBX)[1 + nx*k] = 350/x_sca[1];

  // Initial condition
  ms.input(OCP_LBX)[0] = ms.input(OCP_UBX)[0] = 0/x_sca[0];
  ms.input(OCP_LBX)[1] = ms.input(OCP_UBX)[1] = 250.052/x_sca[1];
  ms.input(OCP_LBX)[2] = ms.input(OCP_UBX)[2] = 956.271/x_sca[2];
  
  integrator.setInput(ocp.t0, INTEGRATOR_T0);
  integrator.setInput(ocp.tf, INTEGRATOR_TF);
  integrator.setInput(x0, INTEGRATOR_X0);
  integrator.setInput(u0, INTEGRATOR_P);
  integrator.setFwdSeed(1.0, INTEGRATOR_P);
  integrator.evaluate(1,0);
  cout << integrator.output() << endl;
  cout << integrator.fwdSens() << endl;
  
  integrator2.setInput(ocp.t0, INTEGRATOR_T0);
  integrator2.setInput(ocp.tf, INTEGRATOR_TF);
  integrator2.setInput(x0, INTEGRATOR_X0);
  integrator2.setInput(u0, INTEGRATOR_P);
  integrator2.setFwdSeed(1.0, INTEGRATOR_P);
  integrator2.evaluate(1,0);
  cout << integrator2.output() << endl;
  cout << integrator2.fwdSens() << endl;
  
//  return 0;
  
  
  IpoptSolver solver(ms.getF(),ms.getG(),FX(),ms.getJ());
  //IpoptSolver solver(ms.F_,ms.G_,FX(),J);
  solver.setOption("tol",1e-5);
  solver.setOption("hessian_approximation", "limited-memory");
  solver.setOption("max_iter",50);
  solver.setOption("linear_solver","ma57");
//  solver.setOption("derivative_test","first-order");

//  solver.setOption("verbose",true);
  solver.init();

  // Pass to ocp solver
  ms.setNLPSolver(solver);
  
  // Solve the problem
  ms.evaluate(0,0);
  
  cout << solver.output() << endl;
  
  
  return 0;
}
