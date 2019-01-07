#include "multiple_shooting_with_saturation.hpp"


MultipleShootingWithSaturation::MultipleShootingWithSaturation(const NMPCModel model, const ControlInputSaturationSequence control_input_saturation_seq, const double horizon_max_length, const double alpha, const int horizon_division_num, const double difference_increment, const double zeta, const int dim_krylov) : MatrixFreeGMRES((model.dimControlInput()+model.dimConstraints())*horizon_division_num, dim_krylov)
{
    // Set dimensions and parameters.
    model_ = model;
    control_input_saturation_seq_ = control_input_saturation_seq;
    dim_state_ = model_.dimState();
    dim_control_input_ = model_.dimControlInput();
    dim_constraints_ = model_.dimConstraints();
    dim_control_input_and_constraints_ = dim_control_input_ + dim_constraints_;
    dim_state_and_lambda_ = 2*dim_state_;
    dim_control_input_and_constraints_seq_ = horizon_division_num * dim_control_input_and_constraints_;
    dim_state_and_lambda_seq_ = horizon_division_num * dim_state_and_lambda_;
    dim_saturation_ = control_input_saturation_seq_.dimSaturation();
    dim_saturation_seq_ = horizon_division_num * dim_saturation_;

    // Set parameters for horizon and the C/GMRES.
    horizon_max_length_ = horizon_max_length;
    alpha_ = alpha;
    horizon_division_num_ = horizon_division_num;
    difference_increment_ = difference_increment;
    zeta_ = zeta;
    dim_krylov_ = dim_krylov;

    // Allocate vectors and matrices.
    dx_vec_.resize(dim_state_);
    incremented_state_vec_.resize(dim_state_);
    control_input_and_constraints_seq_.resize(dim_control_input_and_constraints_seq_);
    incremented_control_input_and_constraints_seq_.resize(dim_control_input_and_constraints_seq_);
    control_input_and_constraints_error_seq_.resize(dim_control_input_and_constraints_seq_);
    control_input_and_constraints_error_seq_1_.resize(dim_control_input_and_constraints_seq_);
    control_input_and_constraints_error_seq_2_.resize(dim_control_input_and_constraints_seq_);
    control_input_and_constraints_error_seq_3_.resize(dim_control_input_and_constraints_seq_);
    control_input_and_constraints_update_seq_.resize(dim_control_input_and_constraints_seq_);

    state_mat_.resize(dim_state_, horizon_division_num);
    lambda_mat_.resize(dim_state_, horizon_division_num);
    incremented_state_mat_.resize(dim_state_, horizon_division_num);
    incremented_lambda_mat_.resize(dim_state_, horizon_division_num);
    state_error_mat_.resize(dim_state_, horizon_division_num);
    state_error_mat_1_.resize(dim_state_, horizon_division_num);
    lambda_error_mat_.resize(dim_state_, horizon_division_num);
    lambda_error_mat_1_.resize(dim_state_, horizon_division_num);
    state_update_mat_.resize(dim_state_, horizon_division_num);
    lambda_update_mat_.resize(dim_state_, horizon_division_num);
    dummy_input_mat_.resize(dim_saturation_, horizon_division_num_);
    saturation_lagrange_multiplier_mat_.resize(dim_saturation_, horizon_division_num_);
    dummy_error_mat_.resize(dim_saturation_, horizon_division_num_);
    saturation_error_mat_.resize(dim_saturation_, horizon_division_num_);
    dummy_error_mat_1_.resize(dim_saturation_, horizon_division_num_);
    saturation_error_mat_1_.resize(dim_saturation_, horizon_division_num_);
    dummy_update_mat_.resize(dim_saturation_, horizon_division_num_);
    saturation_update_mat_.resize(dim_saturation_, horizon_division_num_);


    // Initialize solution of the forward-difference GMRES.
    control_input_and_constraints_update_seq_ = Eigen::VectorXd::Zero(dim_control_input_and_constraints_seq_);
    state_update_mat_ = Eigen::MatrixXd::Zero(dim_state_, horizon_division_num);
    lambda_update_mat_ = Eigen::MatrixXd::Zero(dim_state_, horizon_division_num);
}



void MultipleShootingWithSaturation::initSolution(const double initial_time, const Eigen::VectorXd& initial_state_vec, const Eigen::VectorXd& initial_guess_input_vec, const double convergence_radius, const int max_iteration)
{
    Eigen::VectorXd initial_solution_vec(dim_control_input_and_constraints_+2*dim_saturation_), initial_lambda_vec(dim_state_);
    InitCGMRESWithSaturation initializer(model_, control_input_saturation_seq_, difference_increment_, dim_krylov_);

    // Intialize the solution
    initial_time_ = initial_time;
    initializer.solve0stepNOCP(initial_time, initial_state_vec, initial_guess_input_vec, convergence_radius, max_iteration, initial_solution_vec);

    model_.phixFunc(initial_time, initial_state_vec, initial_lambda_vec);
    for(int i=0; i<horizon_division_num_; i++){
        control_input_and_constraints_seq_.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_) = initial_solution_vec.segment(0, dim_control_input_and_constraints_);
        dummy_input_mat_.col(i) = initial_solution_vec.segment(dim_control_input_and_constraints_, dim_saturation_);
        saturation_lagrange_multiplier_mat_.col(i) = initial_solution_vec.segment(dim_control_input_and_constraints_+dim_saturation_, dim_saturation_);
        state_mat_.col(i) = initial_state_vec;
        lambda_mat_.col(i) = initial_lambda_vec;
    }

    // Intialize the optimality error.
    Eigen::VectorXd initial_control_input_and_constraints_error(dim_control_input_and_constraints_), initial_dummy_input_error(dim_saturation_), initial_saturation_error(dim_saturation_);
    initial_control_input_and_constraints_error = initializer.getControlInputAndConstraintsError(initial_time, initial_state_vec, initial_solution_vec);
    initial_dummy_input_error = initializer.getDummyInputError(initial_time, initial_state_vec, initial_solution_vec);
    initial_saturation_error= initializer.getControlInputSaturationError(initial_time, initial_state_vec, initial_solution_vec);

    for(int i=0; i<horizon_division_num_; i++){
        control_input_and_constraints_error_seq_.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_) = initial_control_input_and_constraints_error;
        dummy_error_mat_.col(i) = initial_dummy_input_error;
        saturation_error_mat_.col(i) = initial_saturation_error;
    }

    state_error_mat_ = Eigen::MatrixXd::Zero(dim_state_, horizon_division_num_);
    lambda_error_mat_ = Eigen::MatrixXd::Zero(dim_state_, horizon_division_num_);
}


void MultipleShootingWithSaturation::initSolution(const double initial_time, const Eigen::VectorXd& initial_state_vec, const Eigen::VectorXd& initial_guess_input_vec, const Eigen::VectorXd& initial_guess_lagrange_multiplier, const double convergence_radius, const int max_iteration)
{
    Eigen::VectorXd initial_solution_vec(dim_control_input_and_constraints_+2*dim_saturation_), initial_lambda_vec(dim_state_);
    InitCGMRESWithSaturation initializer(model_, control_input_saturation_seq_, difference_increment_, dim_krylov_);

    // Intialize the solution
    initial_time_ = initial_time;
    initializer.solve0stepNOCP(initial_time, initial_state_vec, initial_guess_input_vec, initial_guess_lagrange_multiplier, convergence_radius, max_iteration, initial_solution_vec);

    model_.phixFunc(initial_time, initial_state_vec, initial_lambda_vec);
    for(int i=0; i<horizon_division_num_; i++){
        control_input_and_constraints_seq_.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_) = initial_solution_vec.segment(0, dim_control_input_and_constraints_);
        dummy_input_mat_.col(i) = initial_solution_vec.segment(dim_control_input_and_constraints_, dim_saturation_);
        saturation_lagrange_multiplier_mat_.col(i) = initial_solution_vec.segment(dim_control_input_and_constraints_+dim_saturation_, dim_saturation_);
        state_mat_.col(i) = initial_state_vec;
        lambda_mat_.col(i) = initial_lambda_vec;
    }

    // Intialize the optimality error.
    Eigen::VectorXd initial_control_input_and_constraints_error(dim_control_input_and_constraints_), initial_dummy_input_error(dim_saturation_), initial_saturation_error(dim_saturation_);
    initial_control_input_and_constraints_error = initializer.getControlInputAndConstraintsError(initial_time, initial_state_vec, initial_solution_vec);
    initial_dummy_input_error = initializer.getDummyInputError(initial_time, initial_state_vec, initial_solution_vec);
    initial_saturation_error= initializer.getControlInputSaturationError(initial_time, initial_state_vec, initial_solution_vec);

    for(int i=0; i<horizon_division_num_; i++){
        control_input_and_constraints_error_seq_.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_) = initial_control_input_and_constraints_error;
        dummy_error_mat_.col(i) = initial_dummy_input_error;
        saturation_error_mat_.col(i) = initial_saturation_error;
    }

    state_error_mat_ = Eigen::MatrixXd::Zero(dim_state_, horizon_division_num_);
    lambda_error_mat_ = Eigen::MatrixXd::Zero(dim_state_, horizon_division_num_);
}



void MultipleShootingWithSaturation::initSolution(const double initial_time, const Eigen::VectorXd& initial_state_vec, const Eigen::VectorXd& initial_guess_input_vec, const double initial_guess_lagrange_multiplier, const double convergence_radius, const int max_iteration)
{
    Eigen::VectorXd initial_solution_vec(dim_control_input_and_constraints_+2*dim_saturation_), initial_lambda_vec(dim_state_);
    InitCGMRESWithSaturation initializer(model_, control_input_saturation_seq_, difference_increment_, dim_krylov_);

    // Intialize the solution
    initial_time_ = initial_time;
    initializer.solve0stepNOCP(initial_time, initial_state_vec, initial_guess_input_vec, initial_guess_lagrange_multiplier, convergence_radius, max_iteration, initial_solution_vec);

    model_.phixFunc(initial_time, initial_state_vec, initial_lambda_vec);
    for(int i=0; i<horizon_division_num_; i++){
        control_input_and_constraints_seq_.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_) = initial_solution_vec.segment(0, dim_control_input_and_constraints_);
        dummy_input_mat_.col(i) = initial_solution_vec.segment(dim_control_input_and_constraints_, dim_saturation_);
        saturation_lagrange_multiplier_mat_.col(i) = initial_solution_vec.segment(dim_control_input_and_constraints_+dim_saturation_, dim_saturation_);
        state_mat_.col(i) = initial_state_vec;
        lambda_mat_.col(i) = initial_lambda_vec;
    }

    // Intialize the optimality error.
    Eigen::VectorXd initial_control_input_and_constraints_error(dim_control_input_and_constraints_), initial_dummy_input_error(dim_saturation_), initial_saturation_error(dim_saturation_);
    initial_control_input_and_constraints_error = initializer.getControlInputAndConstraintsError(initial_time, initial_state_vec, initial_solution_vec);
    initial_dummy_input_error = initializer.getDummyInputError(initial_time, initial_state_vec, initial_solution_vec);
    initial_saturation_error= initializer.getControlInputSaturationError(initial_time, initial_state_vec, initial_solution_vec);

    for(int i=0; i<horizon_division_num_; i++){
        control_input_and_constraints_error_seq_.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_) = initial_control_input_and_constraints_error;
        dummy_error_mat_.col(i) = initial_dummy_input_error;
        saturation_error_mat_.col(i) = initial_saturation_error;
    }

    state_error_mat_ = Eigen::MatrixXd::Zero(dim_state_, horizon_division_num_);
    lambda_error_mat_ = Eigen::MatrixXd::Zero(dim_state_, horizon_division_num_);
}



void MultipleShootingWithSaturation::controlUpdate(const double current_time, const double sampling_period, const Eigen::VectorXd& current_state_vec, Eigen::Ref<Eigen::VectorXd> optimal_control_input_vec)
{
    // Predict the incremented state.
    incremented_time_ = current_time + difference_increment_;
    model_.stateFunc(current_time, current_state_vec, control_input_and_constraints_seq_.segment(0, dim_control_input_), dx_vec_);
    incremented_state_vec_ = current_state_vec + difference_increment_*dx_vec_;

    forwardDifferenceGMRES(current_time, current_state_vec, control_input_and_constraints_seq_, control_input_and_constraints_update_seq_);

    // Update state_mat_ and lamdba_mat_ by the difference approximation.
    computeStateAndLambda(incremented_time_, incremented_state_vec_, control_input_and_constraints_seq_+difference_increment_*control_input_and_constraints_update_seq_, (1-difference_increment_*zeta_)*state_error_mat_, (1-difference_increment_*zeta_)*lambda_error_mat_, incremented_state_mat_, incremented_lambda_mat_);
    state_update_mat_ = (incremented_state_mat_ - state_mat_)/difference_increment_;
    lambda_update_mat_ = (incremented_lambda_mat_ - lambda_mat_)/difference_increment_;
    state_mat_ += sampling_period * state_update_mat_;
    lambda_mat_ += sampling_period * lambda_update_mat_;

    // std::cout << state_mat_ << std::endl;
    // std::cout << lambda_mat_ << std::endl;

    // Update dummy_input_mat_ and saturation_lagrange_multiplier_mat_
    computeOptimalityErrorforSaturation(control_input_and_constraints_seq_, dummy_input_mat_, saturation_lagrange_multiplier_mat_, dummy_error_mat_, saturation_error_mat_);
    multiplySaturationDerivativeWithControlInput(control_input_and_constraints_seq_, control_input_and_constraints_update_seq_, dummy_error_mat_1_, saturation_error_mat_1_);
    multiplySaturationSelfDerivativeInverse(control_input_and_constraints_seq_, dummy_input_mat_, saturation_lagrange_multiplier_mat_, -zeta_*dummy_error_mat_-dummy_error_mat_1_, -zeta_*saturation_error_mat_-saturation_error_mat_1_, dummy_update_mat_, saturation_update_mat_);
    dummy_input_mat_ += sampling_period * dummy_update_mat_;
    saturation_lagrange_multiplier_mat_ += sampling_period * saturation_update_mat_;

    // std::cout << dummy_input_mat_ << std::endl;
    // std::cout << saturation_lagrange_multiplier_mat_ << std::endl;

    // Update control_input_and_constraints_seq_.
    control_input_and_constraints_seq_ += sampling_period * control_input_and_constraints_update_seq_;

    // std::cout << control_input_and_constraints_seq_ << std::endl;
    // std::exit(1);

    optimal_control_input_vec = control_input_and_constraints_seq_.segment(0, dim_control_input_);
}


void MultipleShootingWithSaturation::getControlInput(Eigen::Ref<Eigen::VectorXd> control_input_vec) const
{
    control_input_vec = control_input_and_constraints_seq_.segment(0, dim_control_input_);
}



double MultipleShootingWithSaturation::getError(const double current_time, const Eigen::VectorXd& current_state_vec)
{
    Eigen::VectorXd control_input_and_constraints_error_seq(dim_control_input_and_constraints_seq_); 
    Eigen::MatrixXd state_error_mat(dim_state_, horizon_division_num_), lambda_error_mat(dim_state_, horizon_division_num_), dummy_error_mat(dim_saturation_, horizon_division_num_), saturation_error_mat(dim_saturation_, horizon_division_num_);


    computeOptimalityErrorforControlInputAndConstraints(current_time, current_state_vec, control_input_and_constraints_seq_, state_mat_, lambda_mat_, saturation_lagrange_multiplier_mat_, control_input_and_constraints_error_seq);

    computeOptimalityErrorforStateAndLambda(current_time, current_state_vec, control_input_and_constraints_seq_, state_mat_, lambda_mat_, state_error_mat, lambda_error_mat);

    computeOptimalityErrorforSaturation(control_input_and_constraints_seq_, dummy_input_mat_, saturation_lagrange_multiplier_mat_, dummy_error_mat, saturation_error_mat);


    double squared_error = control_input_and_constraints_error_seq.squaredNorm();
    for(int i=0; i<horizon_division_num_; i++){
        squared_error += (state_error_mat.col(i).squaredNorm() + lambda_error_mat.col(i).squaredNorm() + dummy_error_mat.col(i).squaredNorm() + saturation_error_mat.col(i).squaredNorm());
    }

    return std::sqrt(squared_error);
}


inline void MultipleShootingWithSaturation::addDerivativeSaturationWithControlInput(const Eigen::VectorXd& control_input_and_constraints_vec, const Eigen::VectorXd& saturation_lagrange_multiplier_vec, Eigen::Ref<Eigen::VectorXd> optimality_for_control_input_and_constraints_vec)
{
    for(int i=0; i<dim_saturation_; i++){
        optimality_for_control_input_and_constraints_vec(control_input_saturation_seq_.index(i)) += (2*control_input_and_constraints_vec(control_input_saturation_seq_.index(i)) - control_input_saturation_seq_.min(i) - control_input_saturation_seq_.max(i)) * saturation_lagrange_multiplier_vec(i);
    }
}


inline void MultipleShootingWithSaturation::computeOptimalityErrorforControlInputAndConstraints(const double time_param, const Eigen::VectorXd& state_vec, const Eigen::VectorXd& control_input_and_constraints_seq, const Eigen::MatrixXd& state_mat, const Eigen::MatrixXd& lambda_mat, const Eigen::MatrixXd& saturation_lagrange_multiplier_mat, Eigen::Ref<Eigen::VectorXd> optimality_for_control_input_and_constraints)
{
    // Set and discretize the horizon.
    double horizon_length = horizon_max_length_ * (1.0 - std::exp(- alpha_ * (time_param - initial_time_)));
    double delta_tau = horizon_length / horizon_division_num_;

    model_.huFunc(time_param, state_vec, control_input_and_constraints_seq.segment(0, dim_control_input_and_constraints_), lambda_mat.col(0), optimality_for_control_input_and_constraints.segment(0, dim_control_input_and_constraints_));
    addDerivativeSaturationWithControlInput(control_input_and_constraints_seq.segment(0, dim_control_input_and_constraints_), saturation_lagrange_multiplier_mat.col(0), optimality_for_control_input_and_constraints.segment(0, dim_control_input_and_constraints_));

    double tau = time_param+delta_tau;
    for(int i=1; i<horizon_division_num_; i++, tau+=delta_tau){
        model_.huFunc(tau, state_mat.col(i-1), control_input_and_constraints_seq.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_), lambda_mat.col(i), optimality_for_control_input_and_constraints.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_));
        addDerivativeSaturationWithControlInput(control_input_and_constraints_seq.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_), saturation_lagrange_multiplier_mat.col(i), optimality_for_control_input_and_constraints.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_));
    }
}


inline void MultipleShootingWithSaturation::computeOptimalityErrorforStateAndLambda(const double time_param, const Eigen::VectorXd& state_vec, const Eigen::VectorXd& control_input_and_constraints_seq, const Eigen::MatrixXd& state_mat, const Eigen::MatrixXd& lambda_mat, Eigen::Ref<Eigen::MatrixXd> optimality_for_state, Eigen::Ref<Eigen::MatrixXd> optimality_for_lambda)
{
    // Set and discretize the horizon.
    double horizon_length = horizon_max_length_ * (1.0 - std::exp(- alpha_ * (time_param - initial_time_)));
    double delta_tau = horizon_length / horizon_division_num_;

    // Compute optimality error for state.
    model_.stateFunc(time_param, state_vec, control_input_and_constraints_seq.segment(0, dim_control_input_), dx_vec_);
    optimality_for_state.col(0) = state_mat.col(0) - state_vec - delta_tau * dx_vec_;
    double tau = time_param + delta_tau;
    for(int i=1; i<horizon_division_num_; i++, tau+=delta_tau){
        model_.stateFunc(tau, state_mat.col(i-1), control_input_and_constraints_seq.segment(i*dim_control_input_and_constraints_, dim_control_input_), dx_vec_);
        optimality_for_state.col(i) = state_mat.col(i) - state_mat.col(i-1) - delta_tau * dx_vec_;
    }

    // Compute optimality error for lambda.
    model_.phixFunc(tau, state_mat.col(horizon_division_num_-1), dx_vec_);
    optimality_for_lambda.col(horizon_division_num_-1) = lambda_mat.col(horizon_division_num_-1) - dx_vec_;
    for(int i=horizon_division_num_-1; i>=1; i--, tau-=delta_tau){
        model_.hxFunc(tau, state_mat.col(i-1), control_input_and_constraints_seq.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_), lambda_mat.col(i), dx_vec_);
        optimality_for_lambda.col(i-1) = lambda_mat.col(i-1) - lambda_mat.col(i) - delta_tau * dx_vec_;
    }
}


inline void MultipleShootingWithSaturation::computeStateAndLambda(const double time_param, const Eigen::VectorXd& state_vec, const Eigen::VectorXd& control_input_and_constraints_seq, const Eigen::MatrixXd& optimality_for_state, const Eigen::MatrixXd& optimality_for_lambda, Eigen::Ref<Eigen::MatrixXd> state_mat, Eigen::Ref<Eigen::MatrixXd> lambda_mat)
{
    // Set and discretize the horizon.
    double horizon_length = horizon_max_length_ * (1.0 - std::exp(- alpha_ * (time_param - initial_time_)));
    double delta_tau = horizon_length / horizon_division_num_;

    // Compute the sequence of state under the error for state.
    model_.stateFunc(time_param, state_vec, control_input_and_constraints_seq.segment(0, dim_control_input_), dx_vec_);
    state_mat.col(0) = state_vec + delta_tau * dx_vec_ + optimality_for_state.col(0);
    double tau = time_param + delta_tau;
    for(int i=1; i<horizon_division_num_; i++, tau+=delta_tau){
        model_.stateFunc(tau, state_mat.col(i-1), control_input_and_constraints_seq.segment(i*dim_control_input_and_constraints_, dim_control_input_), dx_vec_);
        state_mat.col(i) = state_mat.col(i-1) + delta_tau * dx_vec_ + optimality_for_state.col(i);
    }

    // Compute the sequence of lambda under the error for lambda.
    model_.phixFunc(tau, state_mat.col(horizon_division_num_-1), dx_vec_);
    lambda_mat.col(horizon_division_num_-1) = dx_vec_ + optimality_for_lambda.col(horizon_division_num_-1);
    for(int i=horizon_division_num_-1; i>=1; i--, tau-=delta_tau){
        model_.hxFunc(tau, state_mat.col(i-1), control_input_and_constraints_seq.segment(i*dim_control_input_and_constraints_, dim_control_input_and_constraints_), lambda_mat.col(i), dx_vec_);
        lambda_mat.col(i-1) = lambda_mat.col(i) + delta_tau * dx_vec_ + optimality_for_lambda.col(i-1);
    }
}


inline void MultipleShootingWithSaturation::computeOptimalityErrorforSaturation(const Eigen::VectorXd& control_input_and_constraints_seq, const Eigen::MatrixXd& dummy_input_seq, const Eigen::MatrixXd& saturation_lagrange_multiplier_seq, Eigen::Ref<Eigen::MatrixXd> optimality_for_dummy, Eigen::Ref<Eigen::MatrixXd> optimality_for_saturation)
{
    for(int i=0; i<horizon_division_num_; i++){
        for(int j=0; j<dim_saturation_; j++){
            optimality_for_dummy(j,i) = 2 * saturation_lagrange_multiplier_seq(j,i) * dummy_input_seq(j,i) - control_input_saturation_seq_.weight(j);
        }
    }
    for(int i=0; i<horizon_division_num_; i++){
        for(int j=0; j<dim_saturation_; j++){
            optimality_for_saturation(j,i) = 
            (control_input_and_constraints_seq(i*dim_control_input_and_constraints_+control_input_saturation_seq_.index(j))-(control_input_saturation_seq_.min(j)+control_input_saturation_seq_.max(j))/2) * (control_input_and_constraints_seq(i*dim_control_input_and_constraints_+control_input_saturation_seq_.index(j))-(control_input_saturation_seq_.min(j)+control_input_saturation_seq_.max(j))/2) 
            - (control_input_saturation_seq_.max(j)-control_input_saturation_seq_.min(j)) * (control_input_saturation_seq_.max(j)-control_input_saturation_seq_.min(j))/4 + dummy_input_seq(j,i) * dummy_input_seq(j,i);
        }
    }
}


inline void MultipleShootingWithSaturation::multiplySaturationDerivativeWithControlInput(const Eigen::VectorXd& control_input_and_constraints_seq, const Eigen::VectorXd& multiplied_control_input_and_constraints_vec, Eigen::Ref<Eigen::MatrixXd> optimality_for_dummy, Eigen::Ref<Eigen::MatrixXd> optimality_for_saturation)
{
    for(int i=0; i<horizon_division_num_; i++){
        for(int j=0; j<dim_saturation_; j++){
            optimality_for_dummy(j,i) = 0;
        }
    }
    for(int i=0; i<horizon_division_num_; i++){
        for(int j=0; j<dim_saturation_; j++){
            optimality_for_saturation(j,i) = (2*control_input_and_constraints_seq(i*dim_control_input_and_constraints_+control_input_saturation_seq_.index(j)) - control_input_saturation_seq_.min(j) - control_input_saturation_seq_.max(j)) * multiplied_control_input_and_constraints_vec(i*dim_control_input_and_constraints_+control_input_saturation_seq_.index(j));
        }
    }
}


inline void MultipleShootingWithSaturation::multiplySaturationSelfDerivativeInverse(const Eigen::VectorXd& control_input_and_constraints_seq, const Eigen::MatrixXd& dummy_input_seq, const Eigen::MatrixXd& saturation_lagrange_multiplier_seq, const Eigen::MatrixXd& multiplied_dummy_mat, const Eigen::MatrixXd& multiplied_saturation_mat, Eigen::Ref<Eigen::MatrixXd> optimality_for_dummy, Eigen::Ref<Eigen::MatrixXd> optimality_for_saturation)
{
    for(int i=0; i<horizon_division_num_; i++){
        for(int j=0; j<dim_saturation_; j++){
            optimality_for_dummy(j,i) = multiplied_saturation_mat(j,i)/(2*dummy_input_seq(j,i));
        }
    }
    for(int i=0; i<horizon_division_num_; i++){
        for(int j=0; j<dim_saturation_; j++){
            // optimality_for_saturation(j,i) = multiplied_dummy_mat(j,i)/dummy_input_seq(j,i) - multiplied_saturation_mat(j,i) * (saturation_lagrange_multiplier_seq(j,i)+control_input_saturation_seq_.weight(j))/(dummy_input_seq(j,i)*dummy_input_seq(j,i));
            optimality_for_saturation(j,i) = multiplied_dummy_mat(j,i)/(2*dummy_input_seq(j,i)) - multiplied_saturation_mat(j,i) * saturation_lagrange_multiplier_seq(j,i)/(2*dummy_input_seq(j,i)*dummy_input_seq(j,i));
        }
    }
}


void MultipleShootingWithSaturation::bFunc(const double time_param, const Eigen::VectorXd& state_vec, const Eigen::VectorXd& current_solution_vec, Eigen::Ref<Eigen::VectorXd> equation_error_vec)
{
    computeOptimalityErrorforControlInputAndConstraints(time_param, state_vec, current_solution_vec, state_mat_, lambda_mat_, saturation_lagrange_multiplier_mat_, control_input_and_constraints_error_seq_);
    computeOptimalityErrorforControlInputAndConstraints(incremented_time_, incremented_state_vec_, current_solution_vec, state_mat_, lambda_mat_, saturation_lagrange_multiplier_mat_, control_input_and_constraints_error_seq_1_);

    computeOptimalityErrorforStateAndLambda(time_param, state_vec, current_solution_vec, state_mat_, lambda_mat_, state_error_mat_, lambda_error_mat_);
    computeOptimalityErrorforStateAndLambda(incremented_time_, incremented_state_vec_, current_solution_vec, state_mat_, lambda_mat_, state_error_mat_1_, lambda_error_mat_1_);

    computeStateAndLambda(incremented_time_, incremented_state_vec_, current_solution_vec, (1-difference_increment_*zeta_)*state_error_mat_, (1-difference_increment_*zeta_)*lambda_error_mat_, incremented_state_mat_, incremented_lambda_mat_);
    computeOptimalityErrorforSaturation(current_solution_vec, dummy_input_mat_, saturation_lagrange_multiplier_mat_, dummy_error_mat_, saturation_error_mat_);
    multiplySaturationSelfDerivativeInverse(control_input_and_constraints_seq_, dummy_input_mat_, saturation_lagrange_multiplier_mat_, -zeta_*dummy_error_mat_, -zeta_*saturation_error_mat_, dummy_error_mat_1_, saturation_error_mat_1_);

    computeOptimalityErrorforControlInputAndConstraints(incremented_time_, incremented_state_vec_, current_solution_vec, incremented_state_mat_, incremented_lambda_mat_, saturation_lagrange_multiplier_mat_+difference_increment_*saturation_error_mat_1_, control_input_and_constraints_error_seq_3_);


    incremented_control_input_and_constraints_seq_ = current_solution_vec + difference_increment_*control_input_and_constraints_update_seq_;
    computeStateAndLambda(incremented_time_, incremented_state_vec_, incremented_control_input_and_constraints_seq_, state_error_mat_1_, lambda_error_mat_1_, incremented_state_mat_, incremented_lambda_mat_);

    multiplySaturationDerivativeWithControlInput(current_solution_vec, control_input_and_constraints_update_seq_, dummy_error_mat_, saturation_error_mat_);
    multiplySaturationSelfDerivativeInverse(current_solution_vec, dummy_input_mat_, saturation_lagrange_multiplier_mat_, dummy_error_mat_, saturation_error_mat_, dummy_error_mat_1_, saturation_error_mat_1_);
    computeOptimalityErrorforControlInputAndConstraints(incremented_time_, incremented_state_vec_, incremented_control_input_and_constraints_seq_, incremented_state_mat_, incremented_lambda_mat_, saturation_lagrange_multiplier_mat_-difference_increment_*saturation_error_mat_1_, control_input_and_constraints_error_seq_2_);

    equation_error_vec = - (zeta_ - 1/difference_increment_) * control_input_and_constraints_error_seq_ - control_input_and_constraints_error_seq_3_/difference_increment_ - (control_input_and_constraints_error_seq_2_-control_input_and_constraints_error_seq_1_)/difference_increment_;
}


void MultipleShootingWithSaturation::axFunc(const double time_param, const Eigen::VectorXd& state_vec, const Eigen::VectorXd& current_solution_vec, const Eigen::VectorXd& direction_vec, Eigen::Ref<Eigen::VectorXd> forward_difference_error_vec)
{
    incremented_control_input_and_constraints_seq_ = current_solution_vec + difference_increment_*direction_vec;

    computeStateAndLambda(incremented_time_, incremented_state_vec_, incremented_control_input_and_constraints_seq_, state_error_mat_1_, lambda_error_mat_1_, incremented_state_mat_, incremented_lambda_mat_);

    multiplySaturationDerivativeWithControlInput(current_solution_vec, direction_vec, dummy_error_mat_, saturation_error_mat_);
    multiplySaturationSelfDerivativeInverse(current_solution_vec, dummy_input_mat_, saturation_lagrange_multiplier_mat_, dummy_error_mat_, saturation_error_mat_, dummy_error_mat_1_, saturation_error_mat_1_);

    computeOptimalityErrorforControlInputAndConstraints(incremented_time_, incremented_state_vec_, incremented_control_input_and_constraints_seq_, incremented_state_mat_, incremented_lambda_mat_, saturation_lagrange_multiplier_mat_-difference_increment_*saturation_error_mat_1_, control_input_and_constraints_error_seq_2_);


    forward_difference_error_vec =(control_input_and_constraints_error_seq_2_ - control_input_and_constraints_error_seq_1_)/difference_increment_;
}