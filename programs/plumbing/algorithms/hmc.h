#ifndef HMC_H
#define HMC_H


#include <sys/time.h>
#include <ctime>



/// A leapfrog step, which can be used as a building block of an
/// integrator.
// The action term class needs to implement two update steps,
// called momentum_step(double eps) and force_step(double eps).
// The force_step is assumed to be numerically more expensive.
template<class action_term>
void leapfrog_step(action_term &at, double eps){
  at.momentum_step(0.5*eps);
  at.force_step(eps);
  at.momentum_step(0.5*eps);
}


/// A second order step, which can be used as a building block of
/// an integrator
template<class action_term>
void O2_step(action_term &at, double eps){
  double zeta = eps*0.1931833275037836;
  double middlestep = eps-2*zeta;
  at.momentum_step(zeta);
  at.force_step(0.5*eps);
  at.momentum_step(middlestep);
  at.force_step(0.5*eps);
  at.momentum_step(zeta);
}




/// The Hybrid Montecarlo algorithm.
// Consists of an integration step following equations of
// motion implemented in the integrator class gt
// and an accept-reject step using the action
//
// The integrator class must implement at least two functions, 
// action() an integrator_step(double eps) 
template<class integrator_type>
void update_hmc(integrator_type &integr, int steps, double traj_length){
  
  static int accepted=0, trajectory=1;
  struct timeval start, end;
  double timing;

  // Draw the momentum
  integr.draw_gaussian_fields();

  // Make a copy of the gauge field in case the update is rejected
  integr.back_up_fields();
  
  gettimeofday(&start, NULL);

  // Calculate the starting action and print
  double start_action = integr.action();
  output0 << "Begin HMC Trajectory " << trajectory << ": Action " 
          << start_action << "\n";

  // Run the integator
  for(int step=0; step < steps; step++){
    integr.step(traj_length/steps);
  }

  // Recalculate the action
  double end_action = integr.action();
  double edS = exp(-(end_action - start_action));

  output0 << "End HMC: Action " << end_action << " "
        << end_action - start_action
        << " exp(-dS) " << edS << "\n";

  // Accept or reject
  if(hila_random() < edS){
    output0 << "Accepted!\n";
    accepted++;
  } else {
    output0 << "Rejected!\n";
    integr.restore_backup();
  }


  output0 << "Acceptance " << accepted << "/" << trajectory 
          << " " << (double)accepted/(double)trajectory << "\n";

  gettimeofday(&end, NULL);
  timing = (double)(end.tv_sec - start.tv_sec) + 1e-6*(end.tv_usec - start.tv_usec);

  output0 << "HMC done in " << timing << " seconds \n";
  trajectory++;
}



#endif
