#include SCTL_INCLUDE(kernel_functions.hpp)
#include SCTL_INCLUDE(tensor.hpp)
#include SCTL_INCLUDE(quadrule.hpp)
#include SCTL_INCLUDE(ompUtils.hpp)
#include SCTL_INCLUDE(profile.hpp)
#include SCTL_INCLUDE(legendre_rule.hpp)

#include <functional>

namespace SCTL_NAMESPACE {

  template <class Real> void LagrangeInterp<Real>::Interpolate(Vector<Real>& wts, const Vector<Real>& src_nds, const Vector<Real>& trg_nds) {
    if (1) {
      Long Nsrc = src_nds.Dim();
      Long Ntrg = trg_nds.Dim();
      if (wts.Dim() != Nsrc*Ntrg) wts.ReInit(Nsrc*Ntrg);

      Matrix<Real> M(Nsrc, Ntrg, wts.begin(), false);
      for (Long i1 = 0; i1 < Ntrg; i1++) {
        Real x = trg_nds[i1];
        for (Integer j = 0; j < Nsrc; j++) {
          Real y = 1;
          for (Integer k = 0; k < Nsrc; k++) {
            y *= (j==k ? 1 : (src_nds[k] - x) / (src_nds[k] - src_nds[j]));
          }
          M[j][i1] = y;
        }
      }
    }
    if (0) { // Barycentric, numerically unstable (TODO: diagnose)
      Long Nsrc = src_nds.Dim();
      Long Ntrg = trg_nds.Dim();
      if (wts.Dim() != Nsrc*Ntrg) wts.ReInit(Nsrc*Ntrg);
      if (!wts.Dim()) return;
      for (Long t = 0; t < Ntrg; t++) {
        Real scal = 0;
        Long overlap = -1;
        for (Long s = 0; s < Nsrc; s++) {
          if (src_nds[s] == trg_nds[t]) overlap = s;
          scal += 1.0/(src_nds[s]-trg_nds[t]) * (s%2?1.0:-1.0) * ((s==0)||(s==Nsrc-1)?0.5:1.0);
        }
        scal = 1.0 / scal;

        if (overlap == -1) {
          for (Long s = 0; s < Nsrc; s++) {
            wts[s*Ntrg+t] = 1.0/(src_nds[s]-trg_nds[t]) * (s%2?1.0:-1.0) * ((s==0)||(s==Nsrc-1)?0.5:1.0) * scal;
          }
        } else {
          for (Long s = 0; s < Nsrc; s++) wts[s*Ntrg+t] = 0;
          wts[overlap*Ntrg+t] = 1;
        }
      }
    }
  }

  template <class Real> void LagrangeInterp<Real>::Derivative(Vector<Real>& df, const Vector<Real>& f, const Vector<Real>& nds) {
    Long N = nds.Dim();
    Long dof = f.Dim() / N;
    SCTL_ASSERT(f.Dim() == N * dof);
    if (df.Dim() != N * dof) df.ReInit(N * dof);
    if (N*dof == 0) return;

    auto dp = [&nds,&N](Real x, Long i) {
      Real scal = 1;
      for (Long j = 0; j < N; j++) {
        if (i!=j) scal *= (nds[i] - nds[j]);
      }
      scal = 1/scal;
      Real wt = 0;
      for (Long k = 0; k < N; k++) {
        Real wt_ = 1;
        if (k!=i) {
          for (Long j = 0; j < N; j++) {
            if (j!=k && j!=i) wt_ *= (x - nds[j]);
          }
          wt += wt_;
        }
      }
      return wt * scal;
    };
    for (Long k = 0; k < dof; k++) {
      for (Long i = 0; i < N; i++) {
        Real df_ = 0;
        for (Long j = 0; j < N; j++) {
          df_ += f[k*N+j] * dp(nds[i],j);
        }
        df[k*N+i] = df_;
      }
    }
  }

  template <class Real> void LagrangeInterp<Real>::test() { // TODO: cleanup
    Matrix<Real> f(1,3);
    f[0][0] = 0; f[0][1] = 1; f[0][2] = 0.5;

    Vector<Real> src, trg;
    for (Long i = 0; i < 3; i++) src.PushBack(i);
    for (Long i = 0; i < 11; i++) trg.PushBack(i*0.2);
    Vector<Real> wts;
    Interpolate(wts,src,trg);
    Matrix<Real> Mwts(src.Dim(), trg.Dim(), wts.begin(), false);
    Matrix<Real> ff = f * Mwts;
    std::cout<<ff<<'\n';

    Vector<Real> df;
    Derivative(df, Vector<Real>(f.Dim(0)*f.Dim(1),f.begin()), src);
    std::cout<<df<<'\n';
  }






  template <class Real, Integer Nm, Integer Nr, Integer Nt> template <class Kernel> void ToroidalGreensFn<Real,Nm,Nr,Nt>::Setup(const Kernel& ker, Real R0) {
    PrecompToroidalGreensFn<QuadReal>(ker, R0);
  }

  template <class Real, Integer Nm, Integer Nr, Integer Nt> template <class Kernel> void ToroidalGreensFn<Real,Nm,Nr,Nt>::BuildOperatorModal(Matrix<Real>& M, const Real x0, const Real x1, const Real x2, const Kernel& ker) const {
    static constexpr Integer KDIM0 = Kernel::SrcDim();
    static constexpr Integer KDIM1 = Kernel::TrgDim();
    static constexpr Integer Nmm = (Nm/2+1)*2;
    static constexpr Integer Ntt = (Nt/2+1)*2;

    StaticArray<Real,2*Nr> buff0;
    StaticArray<Real,Ntt> buff1;
    Vector<Real> r_basis(Nr,buff0,false);
    Vector<Real> interp_r(Nr,buff0+Nr,false);
    Vector<Real> interp_Ntt(Ntt,buff1,false);
    if (M.Dim(0) != KDIM0*Nmm || M.Dim(1) != KDIM1) M.ReInit(KDIM0*Nmm,KDIM1);
    { // Set M
      const Real r = sqrt<Real>(x0*x0 + x1*x1);
      const Real rho = sqrt<Real>((r-R0_)*(r-R0_) + x2*x2);
      if (rho < max_dist*R0_) {
        const Real r_inv = 1/r;
        const Real rho_inv = 1/rho;
        const Real cos_theta = x0*r_inv;
        const Real sin_theta = x1*r_inv;
        const Real cos_phi = x2*rho_inv;
        const Real sin_phi = (r-R0_)*rho_inv;

        { // Set interp_r
          interp_r = 0;
          const Real rho0 = (rho/R0_-min_dist)/(max_dist-min_dist);
          BasisFn<Real>::EvalBasis(r_basis, rho0);
          for (Long i = 0; i < Nr; i++) {
            Real fn_val = 0;
            for (Long j = 0; j < Nr; j++) {
              fn_val += Mnds2coeff1[0][i*Nr+j] * r_basis[j];
            }
            for (Long j = 0; j < Nr; j++) {
              interp_r[j] += Mnds2coeff0[0][i*Nr+j] * fn_val;
            }
          }
        }
        { // Set interp_Ntt
          interp_Ntt[0] = 0.5;
          interp_Ntt[1] = 0.0;
          Complex<Real> exp_t(cos_phi, sin_phi);
          Complex<Real> exp_jt(cos_phi, sin_phi);
          for (Long j = 1; j < Ntt/2; j++) {
            interp_Ntt[j*2+0] = exp_jt.real;
            interp_Ntt[j*2+1] =-exp_jt.imag;
            exp_jt *= exp_t;
          }
        }

        M = 0;
        for (Long j = 0; j < Nr; j++) {
          for (Long k = 0; k < Ntt; k++) {
            Real interp_wt = interp_r[j] * interp_Ntt[k];
            ConstIterator<Real> Ut_ = Ut.begin() + (j*Ntt+k)*KDIM0*Nmm*KDIM1;
            for (Long i = 0; i < KDIM0*Nmm*KDIM1; i++) { // Set M
              M[0][i] += Ut_[i] * interp_wt;
            }
          }
        }
        { // Rotate by theta
          Complex<Real> exp_iktheta(1,0), exp_itheta(cos_theta, -sin_theta);
          for (Long k = 0; k < Nmm/2; k++) {
            for (Long i = 0; i < KDIM0; i++) {
              for (Long j = 0; j < KDIM1; j++) {
                Complex<Real> c(M[i*Nmm+2*k+0][j],M[i*Nmm+2*k+1][j]);
                c *= exp_iktheta;
                M[i*Nmm+2*k+0][j] = c.real;
                M[i*Nmm+2*k+1][j] = c.imag;
              }
            }
            exp_iktheta *= exp_itheta;
          }
        }
      } else if (rho < max_dist*R0_*1.25) {
        BuildOperatorModalDirect<110>(M, x0, x1, x2, ker);
      } else if (rho < max_dist*R0_*1.67) {
        BuildOperatorModalDirect<88>(M, x0, x1, x2, ker);
      } else if (rho < max_dist*R0_*2.5) {
        BuildOperatorModalDirect<76>(M, x0, x1, x2, ker);
      } else if (rho < max_dist*R0_*5) {
        BuildOperatorModalDirect<50>(M, x0, x1, x2, ker);
      } else if (rho < max_dist*R0_*10) {
        BuildOperatorModalDirect<25>(M, x0, x1, x2, ker);
      } else if (rho < max_dist*R0_*20) {
        BuildOperatorModalDirect<14>(M, x0, x1, x2, ker);
      } else {
        BuildOperatorModalDirect<Nm>(M, x0, x1, x2, ker);
      }
    }
  }

  template <class Real, Integer Nm, Integer Nr, Integer Nt> template <class ValueType> ValueType ToroidalGreensFn<Real,Nm,Nr,Nt>::BasisFn<ValueType>::Eval(const Vector<ValueType>& coeff, ValueType x) {
    if (1) {
      ValueType sum = 0;
      ValueType log_x = log(x);
      Long Nsplit = std::max<Long>(0,(coeff.Dim()-1)/2);
      ValueType x_i = 1;
      for (Long i = 0; i < Nsplit; i++) {
        sum += coeff[i] * x_i;
        x_i *= x;
      }
      x_i = 1;
      for (Long i = coeff.Dim()-2; i >= Nsplit; i--) {
        sum += coeff[i] * log_x * x_i;
        x_i *= x;
      }
      if (coeff.Dim()-1 >= 0) sum += coeff[coeff.Dim()-1] / x;
      return sum;
    }
    if (0) {
      ValueType sum = 0;
      Long Nsplit = coeff.Dim()/2;
      for (Long i = 0; i < Nsplit; i++) {
        sum += coeff[i] * sctl::pow<ValueType,Long>(x,i);
      }
      for (Long i = Nsplit; i < coeff.Dim(); i++) {
        sum += coeff[i] * log(x) * sctl::pow<ValueType,Long>(x,coeff.Dim()-1-i);
      }
      return sum;
    }
  }
  template <class Real, Integer Nm, Integer Nr, Integer Nt> template <class ValueType> void ToroidalGreensFn<Real,Nm,Nr,Nt>::BasisFn<ValueType>::EvalBasis(Vector<ValueType>& f, ValueType x) {
    const Long N = f.Dim();
    const Long Nsplit = std::max<Long>(0,(N-1)/2);

    ValueType xi = 1;
    for (Long i = 0; i < Nsplit; i++) {
      f[i] = xi;
      xi *= x;
    }

    ValueType xi_logx = log(x);
    for (Long i = N-2; i >= Nsplit; i--) {
      f[i] = xi_logx;
      xi_logx *= x;
    }

    if (N-1 >= 0) f[N-1] = 1/x;
  }
  template <class Real, Integer Nm, Integer Nr, Integer Nt> template <class ValueType> const Vector<ValueType>& ToroidalGreensFn<Real,Nm,Nr,Nt>::BasisFn<ValueType>::nds(Integer ORDER) {
    ValueType fn_start = 1e-7, fn_end = 1.0;
    auto compute_nds = [&ORDER,&fn_start,&fn_end]() {
      Vector<ValueType> nds, wts;
      auto integrands = [&ORDER,&fn_start,&fn_end](const Vector<ValueType>& nds) {
        const Integer K = ORDER;
        const Long N = nds.Dim();
        Matrix<ValueType> M(N,K);
        for (Long j = 0; j < N; j++) {
          Vector<ValueType> f(K,M[j],false);
          EvalBasis(f, nds[j]*(fn_end-fn_start)+fn_start);
        }
        return M;
      };
      InterpQuadRule<ValueType>::Build(nds, wts, integrands, sqrt(machine_eps<ValueType>()), ORDER);
      return nds*(fn_end-fn_start)+fn_start;
    };
    static Vector<ValueType> nds = compute_nds();
    return nds;
  }

  template <class Real, Integer Nm, Integer Nr, Integer Nt> template <class ValueType, class Kernel> void ToroidalGreensFn<Real,Nm,Nr,Nt>::PrecompToroidalGreensFn(const Kernel& ker, ValueType R0) {
    SCTL_ASSERT(ker.CoordDim() == COORD_DIM);
    static constexpr Integer KDIM0 = Kernel::SrcDim();
    static constexpr Integer KDIM1 = Kernel::TrgDim();
    static constexpr Long Nmm = (Nm/2+1)*2;
    static constexpr Long Ntt = (Nt/2+1)*2;
    R0_ = (Real)R0;

    const auto& nds = BasisFn<ValueType>::nds(Nr);
    { // Set Mnds2coeff0, Mnds2coeff1
      Matrix<ValueType> M(Nr,Nr);
      Vector<ValueType> coeff(Nr); coeff = 0;
      for (Long i = 0; i < Nr; i++) {
        coeff[i] = 1;
        for (Long j = 0; j < Nr; j++) {
          M[i][j] = BasisFn<ValueType>::Eval(coeff, nds[j]);
        }
        coeff[i] = 0;
      }

      Matrix<ValueType> U, S, Vt;
      M.SVD(U, S, Vt);
      for (Long i = 0; i < S.Dim(0); i++) {
        S[i][i] = 1/S[i][i];
      }
      auto Mnds2coeff0_ = S * Vt;
      auto Mnds2coeff1_ = U.Transpose();
      Mnds2coeff0.ReInit(Mnds2coeff0_.Dim(0), Mnds2coeff0_.Dim(1));
      Mnds2coeff1.ReInit(Mnds2coeff1_.Dim(0), Mnds2coeff1_.Dim(1));
      for (Long i = 0; i < Mnds2coeff0.Dim(0)*Mnds2coeff0.Dim(1); i++) Mnds2coeff0[0][i] = (Real)Mnds2coeff0_[0][i];
      for (Long i = 0; i < Mnds2coeff1.Dim(0)*Mnds2coeff1.Dim(1); i++) Mnds2coeff1[0][i] = (Real)Mnds2coeff1_[0][i];
    }
    { // Setup fft_Nm_R2C
      Vector<Long> dim_vec(1);
      dim_vec[0] = Nm;
      fft_Nm_R2C.Setup(FFT_Type::R2C, KDIM0, dim_vec);
      fft_Nm_C2R.Setup(FFT_Type::C2R, KDIM0*KDIM1, dim_vec);
    }

    Vector<ValueType> Xtrg(Nr*Nt*COORD_DIM);
    for (Long i = 0; i < Nr; i++) {
      for (Long j = 0; j < Nt; j++) {
        Xtrg[(i*Nt+j)*COORD_DIM+0] = R0 * (1.0 + (min_dist+(max_dist-min_dist)*nds[i]) * sin<ValueType>(j*2*const_pi<ValueType>()/Nt));
        Xtrg[(i*Nt+j)*COORD_DIM+1] = R0 * (0.0);
        Xtrg[(i*Nt+j)*COORD_DIM+2] = R0 * (0.0 + (min_dist+(max_dist-min_dist)*nds[i]) * cos<ValueType>(j*2*const_pi<ValueType>()/Nt));
      }
    }

    Vector<ValueType> U0(KDIM0*Nmm*Nr*KDIM1*Nt);
    { // Set U0
      FFT<ValueType> fft_Nm_C2R;
      { // Setup fft_Nm_C2R
        Vector<Long> dim_vec(1);
        dim_vec[0] = Nm;
        fft_Nm_C2R.Setup(FFT_Type::C2R, KDIM0, dim_vec);
      }
      Vector<ValueType> Fcoeff(KDIM0*Nmm), F, U_;
      for (Long i = 0; i < KDIM0*Nmm; i++) {
        Fcoeff = 0; Fcoeff[i] = 1;
        { // Set F
          fft_Nm_C2R.Execute(Fcoeff, F);
          Matrix<ValueType> FF(KDIM0,Nm,F.begin(), false);
          FF = FF.Transpose();
        }
        ComputePotential<ValueType>(U_, Xtrg, R0, F, ker);
        SCTL_ASSERT(U_.Dim() == Nr*Nt*KDIM1);

        for (Long j = 0; j < Nr; j++) {
          for (Long l = 0; l < Nt; l++) {
            for (Long k = 0; k < KDIM1; k++) {
              U0[((i*Nr+j)*KDIM1+k)*Nt+l] = U_[(j*Nt+l)*KDIM1+k];
            }
          }
        }
      }
    }

    Vector<ValueType> U1(KDIM0*Nmm*Nr*KDIM1*Ntt);
    { // U1 <-- fft_Nt(U0)
      FFT<ValueType> fft_Nt;
      Vector<Long> dim_vec(1); dim_vec = Nt;
      fft_Nt.Setup(FFT_Type::R2C, KDIM0*Nmm*Nr*KDIM1, dim_vec);
      fft_Nt.Execute(U0, U1);
      if (Nt%2==0 && Nt) {
        for (Long i = Ntt-2; i < U1.Dim(); i += Ntt) {
          U1[i] *= 0.5;
        }
      }
      U1 *= 1.0/sqrt<ValueType>(Nt);
    }

    U.ReInit(KDIM0*Nmm*KDIM1*Nr*Ntt);
    { // U <-- rearrange(U1)
      for (Long i0 = 0; i0 < KDIM0*Nmm; i0++) {
        for (Long i1 = 0; i1 < Nr; i1++) {
          for (Long i2 = 0; i2 < KDIM1; i2++) {
            for (Long i3 = 0; i3 < Ntt; i3++) {
              U[((i0*Nr+i1)*KDIM1+i2)*Ntt+i3] = (Real)U1[((i0*KDIM1+i2)*Nr+i1)*Ntt+i3];
            }
          }
        }
      }
    }

    Ut.ReInit(Nr*Ntt*KDIM0*Nmm*KDIM1);
    { // Set Ut
      Matrix<Real> Ut_(Nr*Ntt,KDIM0*Nmm*KDIM1, Ut.begin(), false);
      Matrix<Real> U_(KDIM0*Nmm*KDIM1,Nr*Ntt, U.begin(), false);
      Ut_ = U_.Transpose()*2.0;
    }
  }

  template <class Real, Integer Nm, Integer Nr, Integer Nt> template <class ValueType, class Kernel> void ToroidalGreensFn<Real,Nm,Nr,Nt>::ComputePotential(Vector<ValueType>& U, const Vector<ValueType>& Xtrg, ValueType R0, const Vector<ValueType>& F_, const Kernel& ker, ValueType tol) {
    static constexpr Integer KDIM0 = Kernel::SrcDim();
    Vector<ValueType> F_fourier_coeff;
    const Long Nt_ = F_.Dim() / KDIM0; // number of Fourier modes
    SCTL_ASSERT(F_.Dim() == Nt_ * KDIM0);

    { // Transpose F_
      Matrix<ValueType> FF(Nt_,KDIM0,(Iterator<ValueType>)F_.begin(), false);
      FF = FF.Transpose();
    }
    { // Set F_fourier_coeff
      FFT<ValueType> fft_plan;
      Vector<Long> dim_vec(1); dim_vec[0] = Nt_;
      fft_plan.Setup(FFT_Type::R2C, KDIM0, dim_vec);
      fft_plan.Execute(F_, F_fourier_coeff);
      if (Nt_%2==0 && F_fourier_coeff.Dim()) {
        F_fourier_coeff[F_fourier_coeff.Dim()-2] *= 0.5;
      }
    }
    auto EvalFourierExp = [&Nt_](Vector<ValueType>& F, const Vector<ValueType>& F_fourier_coeff, Integer dof, const Vector<ValueType>& theta) {
      const Long N = F_fourier_coeff.Dim() / dof / 2;
      SCTL_ASSERT(F_fourier_coeff.Dim() == dof * N * 2);
      const Long Ntheta = theta.Dim();
      if (F.Dim() != Ntheta*dof) F.ReInit(Ntheta*dof);
      for (Integer k = 0; k < dof; k++) {
        for (Long j = 0; j < Ntheta; j++) {
          Complex<ValueType> F_(0,0);
          for (Long i = 0; i < N; i++) {
            Complex<ValueType> c(F_fourier_coeff[(k*N+i)*2+0],F_fourier_coeff[(k*N+i)*2+1]);
            Complex<ValueType> exp_t(cos<ValueType>(theta[j]*i), sin<ValueType>(theta[j]*i));
            F_ += exp_t * c * (i==0?1:2);
          }
          F[j*dof+k] = F_.real/sqrt<ValueType>(Nt_);
        }
      }
    };

    static constexpr Integer QuadOrder = 18;
    std::function<Vector<ValueType>(ValueType,ValueType,ValueType)>  compute_potential = [&](ValueType a, ValueType b, ValueType tol) -> Vector<ValueType> {
      auto GetGeomCircle = [&R0] (Vector<ValueType>& Xsrc, Vector<ValueType>& Nsrc, const Vector<ValueType>& nds) {
        Long N = nds.Dim();
        if (Xsrc.Dim() != N * COORD_DIM) Xsrc.ReInit(N*COORD_DIM);
        if (Nsrc.Dim() != N * COORD_DIM) Nsrc.ReInit(N*COORD_DIM);
        for (Long i = 0; i < N; i++) {
          Xsrc[i*COORD_DIM+0] = R0 * cos<ValueType>(nds[i]);
          Xsrc[i*COORD_DIM+1] = R0 * sin<ValueType>(nds[i]);
          Xsrc[i*COORD_DIM+2] = R0 * 0;
          Nsrc[i*COORD_DIM+0] = cos<ValueType>(nds[i]);
          Nsrc[i*COORD_DIM+1] = sin<ValueType>(nds[i]);
          Nsrc[i*COORD_DIM+2] = 0;
        }
      };

      const auto& nds0 = ChebQuadRule<ValueType>::nds(QuadOrder+1);
      const auto& wts0 = ChebQuadRule<ValueType>::wts(QuadOrder+1);
      const auto& nds1 = ChebQuadRule<ValueType>::nds(QuadOrder+0);
      const auto& wts1 = ChebQuadRule<ValueType>::wts(QuadOrder+0);

      Vector<ValueType> U0;
      Vector<ValueType> Xsrc, Nsrc, Fsrc;
      GetGeomCircle(Xsrc, Nsrc, a+(b-a)*nds0);
      EvalFourierExp(Fsrc, F_fourier_coeff, KDIM0, a+(b-a)*nds0);
      for (Long i = 0; i < nds0.Dim(); i++) {
        for (Long j = 0; j < KDIM0; j++) {
          Fsrc[i*KDIM0+j] *= ((b-a) * wts0[i]);
        }
      }
      ker.Eval(U0, Xtrg, Xsrc, Nsrc, Fsrc);

      Vector<ValueType> U1;
      GetGeomCircle(Xsrc, Nsrc, a+(b-a)*nds1);
      EvalFourierExp(Fsrc, F_fourier_coeff, KDIM0, a+(b-a)*nds1);
      for (Long i = 0; i < nds1.Dim(); i++) {
        for (Long j = 0; j < KDIM0; j++) {
          Fsrc[i*KDIM0+j] *= ((b-a) * wts1[i]);
        }
      }
      ker.Eval(U1, Xtrg, Xsrc, Nsrc, Fsrc);

      ValueType err = 0, max_val = 0;
      for (Long i = 0; i < U1.Dim(); i++) {
        err = std::max<ValueType>(err, fabs(U0[i]-U1[i]));
        max_val = std::max<ValueType>(max_val, fabs(U0[i]));
      }
      if (err < tol || (b-a)<tol) {
      //if ((a != 0 && b != 2*const_pi<ValueType>()) || (b-a)<tol) {
        std::cout<<a<<' '<<b-a<<' '<<err<<' '<<tol<<'\n';
        return U1;
      } else {
        U0 = compute_potential(a, (a+b)*0.5, tol);
        U1 = compute_potential((a+b)*0.5, b, tol);
        return U0 + U1;
      }
    };
    U = compute_potential(0, 2*const_pi<ValueType>(), tol);
  };

  template <class Real, Integer Nm, Integer Nr, Integer Nt> template <Integer Nnds, class Kernel> void ToroidalGreensFn<Real,Nm,Nr,Nt>::BuildOperatorModalDirect(Matrix<Real>& M, const Real x0, const Real x1, const Real x2, const Kernel& ker) const {
    static constexpr Integer KDIM0 = Kernel::SrcDim();
    static constexpr Integer KDIM1 = Kernel::TrgDim();
    static constexpr Integer Nmm = (Nm/2+1)*2;

    auto get_sin_theta = [](Long N){
      Vector<Real> sin_theta(N);
      for (Long i = 0; i < N; i++) {
        sin_theta[i] = sin(2*const_pi<Real>()*i/N);
      }
      return sin_theta;
    };
    auto get_cos_theta = [](Long N){
      Vector<Real> cos_theta(N);
      for (Long i = 0; i < N; i++) {
        cos_theta[i] = cos(2*const_pi<Real>()*i/N);
      }
      return cos_theta;
    };
    auto get_circle_coord = [](Long N, Real R0){
      Vector<Real> X(N*COORD_DIM);
      for (Long i = 0; i < N; i++) {
        X[i*COORD_DIM+0] = R0*cos(2*const_pi<Real>()*i/N);
        X[i*COORD_DIM+1] = R0*sin(2*const_pi<Real>()*i/N);
        X[i*COORD_DIM+2] = 0;
      }
      return X;
    };
    static Real scal = 2/sqrt<Real>(Nm);

    static const Vector<Real> sin_nds = get_sin_theta(Nnds);
    static const Vector<Real> cos_nds = get_cos_theta(Nnds);
    static const Vector<Real> Xn = get_circle_coord(Nnds,1);

    StaticArray<Real,Nnds*COORD_DIM> buff0;
    Vector<Real> Xs(Nnds*COORD_DIM,buff0,false);
    Xs = Xn * R0_;

    StaticArray<Real,COORD_DIM> Xt = {x0,x1,x2};
    StaticArray<Real,KDIM0*KDIM1*Nnds> mem_buff2;
    Matrix<Real> Mker(KDIM0*Nnds, KDIM1, mem_buff2, false);
    ker.KernelMatrix(Mker, Vector<Real>(COORD_DIM,(Iterator<Real>)Xt,false), Xs, Xn);

    StaticArray<Real,4*Nnds> mem_buff3;
    Vector<Complex<Real>> exp_itheta(Nnds, (Iterator<Complex<Real>>)(mem_buff3+0*Nnds), false);
    Vector<Complex<Real>> exp_iktheta_da(Nnds, (Iterator<Complex<Real>>)(mem_buff3+2*Nnds), false);
    for (Integer j = 0; j < Nnds; j++) {
      exp_itheta[j].real = cos_nds[j];
      exp_itheta[j].imag =-sin_nds[j];
      exp_iktheta_da[j].real = 2*const_pi<Real>()/Nnds*scal;
      exp_iktheta_da[j].imag = 0;
    }
    for (Integer k = 0; k < Nmm/2; k++) { // apply Mker to complex exponentials
      // TODO: FFT might be faster since points are uniform
      Tensor<Real,true,KDIM0,KDIM1> Mk0, Mk1;
      for (Integer i0 = 0; i0 < KDIM0; i0++) {
        for (Integer i1 = 0; i1 < KDIM1; i1++) {
          Mk0(i0,i1) = 0;
          Mk1(i0,i1) = 0;
        }
      }
      for (Integer j = 0; j < Nnds; j++) {
        Tensor<Real,false,KDIM0,KDIM1> Mker_(Mker[j*KDIM0]);
        Mk0 = Mk0 + Mker_ * exp_iktheta_da[j].real;
        Mk1 = Mk1 + Mker_ * exp_iktheta_da[j].imag;
      }
      for (Integer i0 = 0; i0 < KDIM0; i0++) {
        for (Integer i1 = 0; i1 < KDIM1; i1++) {
          M[i0*Nmm+(k*2+0)][i1] = Mk0(i0,i1);
          M[i0*Nmm+(k*2+1)][i1] = Mk1(i0,i1);
        }
      }
      exp_iktheta_da *= exp_itheta;
    }
    for (Integer i0 = 0; i0 < KDIM0; i0++) {
      for (Integer i1 = 0; i1 < KDIM1; i1++) {
        M[i0*Nmm+0][i1] *= 0.5;
        M[i0*Nmm+1][i1] *= 0.5;
        if (Nm%2 == 0) {
          M[(i0+1)*Nmm-2][i1] *= 0.5;
          M[(i0+1)*Nmm-1][i1] *= 0.5;
        }
      }
    }
  }






  template <class ValueType> static void ReadFile(Vector<Vector<ValueType>>& data, const std::string fname) {
    FILE* f = fopen(fname.c_str(), "r");
    if (f == nullptr) {
      std::cout << "Unable to open file for reading:" << fname << '\n';
    } else {
      uint64_t data_len;
      Long readlen = fread(&data_len, sizeof(uint64_t), 1, f);
      SCTL_ASSERT(readlen == 1);
      if (data_len) {
        data.ReInit(data_len);
        for (Long i = 0; i < data.Dim(); i++) {
          readlen = fread(&data_len, sizeof(uint64_t), 1, f);
          SCTL_ASSERT(readlen == 1);
          data[i].ReInit(data_len);
          if (data_len) {
            readlen = fread(&data[i][0], sizeof(ValueType), data_len, f);
            SCTL_ASSERT(readlen == (Long)data_len);
          }
        }
      }
      fclose(f);
    }
  }
  template <class ValueType> static void WriteFile(const Vector<Vector<ValueType>>& data, const std::string fname) {
    FILE* f = fopen(fname.c_str(), "wb+");
    if (f == nullptr) {
      std::cout << "Unable to open file for writing:" << fname << '\n';
      exit(0);
    }
    uint64_t data_len = data.Dim();
    fwrite(&data_len, sizeof(uint64_t), 1, f);

    for (Integer i = 0; i < data.Dim(); i++) {
      data_len = data[i].Dim();
      fwrite(&data_len, sizeof(uint64_t), 1, f);
      if (data_len) fwrite(&data[i][0], sizeof(ValueType), data_len, f);
    }
    fclose(f);
  }

  template <class ValueType> static ValueType dot_prod(const Tensor<ValueType,true,3,1>& u, const Tensor<ValueType,true,3,1>& v) {
    ValueType u_dot_v = 0;
    u_dot_v += u(0,0) * v(0,0);
    u_dot_v += u(1,0) * v(1,0);
    u_dot_v += u(2,0) * v(2,0);
    return u_dot_v;
  }
  template <class ValueType> static Tensor<ValueType,true,3,1> cross_prod(const Tensor<ValueType,true,3,1>& u, const Tensor<ValueType,true,3,1>& v) {
    Tensor<ValueType,true,3,1> uxv;
    uxv(0,0) = u(1,0) * v(2,0) - u(2,0) * v(1,0);
    uxv(1,0) = u(2,0) * v(0,0) - u(0,0) * v(2,0);
    uxv(2,0) = u(0,0) * v(1,0) - u(1,0) * v(0,0);
    return uxv;
  }

  template <class Real> static const Vector<Real>& sin_theta(const Integer ORDER) {
    constexpr Integer MaxOrder = 100;
    auto compute_sin_theta = [](){
      Vector<Vector<Real>> sin_theta_lst(MaxOrder);
      for (Long k = 0; k < MaxOrder; k++) {
        sin_theta_lst[k].ReInit(k);
        for (Long i = 0; i < k; i++) {
          sin_theta_lst[k][i] = sin<Real>(2*const_pi<Real>()*i/k);
        }
      }
      return sin_theta_lst;
    };
    static const auto sin_theta_lst = compute_sin_theta();

    SCTL_ASSERT(ORDER < MaxOrder);
    return sin_theta_lst[ORDER];
  }
  template <class Real> static const Vector<Real>& cos_theta(const Integer ORDER) {
    constexpr Integer MaxOrder = 100;
    auto compute_cos_theta = [](){
      Vector<Vector<Real>> cos_theta_lst(MaxOrder);
      for (Long k = 0; k < MaxOrder; k++) {
        cos_theta_lst[k].ReInit(k);
        for (Long i = 0; i < k; i++) {
          cos_theta_lst[k][i] = cos<Real>(2*const_pi<Real>()*i/k);
        }
      }
      return cos_theta_lst;
    };
    static const auto cos_theta_lst = compute_cos_theta();

    SCTL_ASSERT(ORDER < MaxOrder);
    return cos_theta_lst[ORDER];
  }
  template <class Real> static const Matrix<Real>& fourier_matrix(Integer Nmodes, Integer Nnodes) {
    constexpr Integer MaxOrder = 50;
    auto compute_fourier_matrix = [](Integer Nmodes, Integer Nnodes) {
      if (Nnodes == 0 || Nmodes == 0) return Matrix<Real>();
      Matrix<Real> M_fourier(2*Nmodes,Nnodes);
      for (Long i = 0; i < Nnodes; i++) {
        Real theta = 2*const_pi<Real>()*i/Nnodes;
        for (Long k = 0; k < Nmodes; k++) {
          M_fourier[k*2+0][i] = cos(k*theta);
          M_fourier[k*2+1][i] = sin(k*theta);
        }
      }
      return M_fourier;
    };
    auto compute_all = [&compute_fourier_matrix]() {
      Matrix<Matrix<Real>> Mall(MaxOrder, MaxOrder);
      for (Long i = 0; i < MaxOrder; i++) {
        for (Long j = 0; j < MaxOrder; j++) {
          Mall[i][j] = compute_fourier_matrix(i,j);
        }
      }
      return Mall;
    };
    static const Matrix<Matrix<Real>> Mall = compute_all();

    SCTL_ASSERT(Nmodes < MaxOrder && Nnodes < MaxOrder);
    return Mall[Nmodes][Nnodes];
  }
  template <class Real> static const Matrix<Real>& fourier_matrix_inv(Integer Nnodes, Integer Nmodes) {
    constexpr Integer MaxOrder = 50;
    auto compute_fourier_matrix_inv = [](Integer Nnodes, Integer Nmodes) {
      if (Nmodes > Nnodes/2+1 || Nnodes == 0 || Nmodes == 0) return Matrix<Real>();
      const Real scal = 2/(Real)Nnodes;

      Matrix<Real> M_fourier_inv(Nnodes,2*Nmodes);
      for (Long i = 0; i < Nnodes; i++) {
        Real theta = 2*const_pi<Real>()*i/Nnodes;
        for (Long k = 0; k < Nmodes; k++) {
          M_fourier_inv[i][k*2+0] = cos(k*theta)*scal;
          M_fourier_inv[i][k*2+1] = sin(k*theta)*scal;
        }
      }
      for (Long i = 0; i < Nnodes; i++) {
        M_fourier_inv[i][0] *= 0.5;
      }
      if (Nnodes == (Nmodes-1)*2) {
        for (Long i = 0; i < Nnodes; i++) {
          M_fourier_inv[i][Nnodes] *= 0.5;
        }
      }
      return M_fourier_inv;
    };
    auto compute_all = [&compute_fourier_matrix_inv]() {
      Matrix<Matrix<Real>> Mall(MaxOrder, MaxOrder);
      for (Long i = 0; i < MaxOrder; i++) {
        for (Long j = 0; j < MaxOrder; j++) {
          Mall[i][j] = compute_fourier_matrix_inv(i,j);
        }
      }
      return Mall;
    };
    static const Matrix<Matrix<Real>> Mall = compute_all();

    SCTL_ASSERT(Nnodes < MaxOrder && Nmodes < MaxOrder);
    return Mall[Nnodes][Nmodes];
  }

  template <class ValueType> static const std::pair<Vector<ValueType>,Vector<ValueType>>& LegendreQuadRule(Integer ORDER) {
    constexpr Integer max_order = 50;
    auto compute_nds_wts = []() {
      Vector<std::pair<Vector<ValueType>,Vector<ValueType>>> nds_wts(max_order);
      for (Integer order = 1; order < max_order; order++) {
        auto& x_ = nds_wts[order].first;
        auto& w_ = nds_wts[order].second;
        if (std::is_same<double,ValueType>::value || std::is_same<float,ValueType>::value) {
          Vector<double> xd(order);
          Vector<double> wd(order);
          int kind = 1;
          double alpha = 0.0, beta = 0.0, a = 0.0, b = 1.0;
          cgqf(order, kind, (double)alpha, (double)beta, (double)a, (double)b, &xd[0], &wd[0]);
          for (Integer i = 0; i < order; i++) {
            x_.PushBack((ValueType)xd[i]);
            w_.PushBack((ValueType)wd[i]);
          }
        } else {
          x_ = ChebQuadRule<ValueType>::nds(order);
          w_ = ChebQuadRule<ValueType>::wts(order);
        }
      }
      return nds_wts;
    };
    static const auto nds_wts = compute_nds_wts();

    SCTL_ASSERT(ORDER < max_order);
    return nds_wts[ORDER];
  }
  template <class ValueType> static const std::pair<Vector<ValueType>,Vector<ValueType>>& LogSingularityQuadRule(Integer ORDER) {
    constexpr Integer MaxOrder = 50;
    auto compute_nds_wts_lst = []() {
      Vector<Vector<QuadReal>> data;
      ReadFile<QuadReal>(data, "log_quad");
      if (data.Dim() != MaxOrder*2) {
        data.ReInit(MaxOrder*2);
        #pragma omp parallel for
        for (Integer order = 1; order < MaxOrder; order++) {
          auto integrands = [order](const Vector<QuadReal>& nds) {
            const Integer K = order;
            const Long N = nds.Dim();
            Matrix<QuadReal> M(N,K);
            for (Long j = 0; j < N; j++) {
              for (Long i = 0; i < K/2; i++) {
                M[j][i] = pow<QuadReal,Long>(nds[j],i);
              }
              for (Long i = K/2; i < K; i++) {
                M[j][i] = pow<QuadReal,Long>(nds[j],K-i-1) * log<QuadReal>(nds[j]);
              }
            }
            return M;
          };
          InterpQuadRule<QuadReal>::Build(data[order*2+0], data[order*2+1], integrands, 1e-20, order, 2e-4, 0.9998); // TODO: diagnose accuracy issues
        }
        WriteFile<QuadReal>(data, "log_quad");
      }

      Vector<std::pair<Vector<ValueType>,Vector<ValueType>>> nds_wts_lst(MaxOrder);
      #pragma omp parallel for
      for (Integer order = 1; order < MaxOrder; order++) {
        const auto& nds = data[order*2+0];
        const auto& wts = data[order*2+1];
        auto& nds_ = nds_wts_lst[order].first;
        auto& wts_ = nds_wts_lst[order].second;
        nds_.ReInit(nds.Dim());
        wts_.ReInit(wts.Dim());
        for (Long i = 0; i < nds.Dim(); i++) {
          nds_[i] = (ValueType)nds[i];
          wts_[i] = (ValueType)wts[i];
        }
      }
      return nds_wts_lst;
    };
    static const auto nds_wts_lst = compute_nds_wts_lst();

    SCTL_ASSERT(ORDER < MaxOrder);
    return nds_wts_lst[ORDER];
  }

  template <class RealType> static Vector<Vector<RealType>> BuildToroidalSpecialQuadRules(Integer Nmodes) {
    constexpr Integer COORD_DIM = 3;
    constexpr Integer max_adap_depth = 30; // build quadrature rules for points up to 2*pi*0.5^max_adap_depth from source loop
    constexpr Integer crossover_adap_depth = 3;
    constexpr Integer max_digits = 20;

    using ValueType = QuadReal;
    Vector<Vector<ValueType>> data;
    const std::string fname = std::string("toroidal_quad_rule_m") + std::to_string(Nmodes);
    ReadFile(data, fname);
    if (data.Dim() != max_adap_depth*max_digits) { // If file is not-found then compute quadrature rule and write to file
      data.ReInit(max_adap_depth * max_digits);
      for (Integer idx = 0; idx < max_adap_depth; idx++) {
        Vector<Vector<ValueType>> quad_nds,  quad_wts;
        { // generate special quadrature rule
          Vector<ValueType> nds, wts;
          Matrix<ValueType> Mintegrands;
          auto discretize_basis_functions = [Nmodes](Matrix<ValueType>& Mintegrands, Vector<ValueType>& nds, Vector<ValueType>& wts, ValueType dist, const std::pair<Vector<ValueType>,Vector<ValueType>>& panel_quad_nds_wts) {
            auto trg_coord = [Nmodes](ValueType dist) {
              Vector<ValueType> Xtrg; //(2*Nmodes*2*Nmodes*COORD_DIM);
              for (Long i = 0; i < 2*Nmodes; i++) {
                for (Long j = 0; j < 2*Nmodes; j++) {
                  ValueType theta = i*2*const_pi<ValueType>()/(2*Nmodes);
                  ValueType r = (0.5 + i*0.5/(2*Nmodes)) * dist;
                  ValueType x0 = r*cos<ValueType>(theta)+1;
                  ValueType x1 = 0;
                  ValueType x2 = r*sin<ValueType>(theta);
                  if (x0 > 0) {
                    Xtrg.PushBack(x0);
                    Xtrg.PushBack(x1);
                    Xtrg.PushBack(x2);
                  }
                }
              }
              return Xtrg;
            };
            Vector<ValueType> Xtrg = trg_coord(dist);
            Long Ntrg = Xtrg.Dim()/COORD_DIM;

            auto adap_nds_wts = [&panel_quad_nds_wts](Vector<ValueType>& nds, Vector<ValueType>& wts, Integer levels){
              const auto& leg_nds = panel_quad_nds_wts.first;
              const auto& leg_wts = panel_quad_nds_wts.second;
              SCTL_ASSERT(levels);
              Long N = 2*levels;
              ValueType l = 0.5;
              nds.ReInit(N*leg_nds.Dim());
              wts.ReInit(N*leg_nds.Dim());
              for (Integer idx = 0; idx < levels-1; idx++) {
                l *= 0.5;
                Vector<ValueType> nds0(leg_nds.Dim(), nds.begin()+(idx*2+0)*leg_nds.Dim(), false);
                Vector<ValueType> nds1(leg_nds.Dim(), nds.begin()+(idx*2+1)*leg_nds.Dim(), false);
                Vector<ValueType> wts0(leg_wts.Dim(), wts.begin()+(idx*2+0)*leg_wts.Dim(), false);
                Vector<ValueType> wts1(leg_wts.Dim(), wts.begin()+(idx*2+1)*leg_wts.Dim(), false);
                for (Long i = 0; i < leg_nds.Dim(); i++) {
                  ValueType s = leg_nds[i]*l + l;
                  nds0[i] = s;
                  nds1[i] = 1-s;
                  wts0[i] = leg_wts[i]*l;
                  wts1[i] = wts0[i];
                }
              }
              { // set nds, wts
                Long idx = levels-1;
                Vector<ValueType> nds0(leg_nds.Dim(), nds.begin()+(idx*2+0)*leg_nds.Dim(), false);
                Vector<ValueType> nds1(leg_nds.Dim(), nds.begin()+(idx*2+1)*leg_nds.Dim(), false);
                Vector<ValueType> wts0(leg_wts.Dim(), wts.begin()+(idx*2+0)*leg_wts.Dim(), false);
                Vector<ValueType> wts1(leg_wts.Dim(), wts.begin()+(idx*2+1)*leg_wts.Dim(), false);
                for (Long i = 0; i < leg_nds.Dim(); i++) {
                  ValueType s = leg_nds[i]*l;
                  nds0[i] = s;
                  nds1[i] = 1-s;
                  wts0[i] = leg_wts[i]*l;
                  wts1[i] = wts0[i];
                }
              }
            };
            adap_nds_wts(nds, wts, std::max<Integer>(1,(Integer)(log(dist/2/const_pi<ValueType>())/log(0.5)+0.5)));

            Long Nnds = nds.Dim();
            Vector<Complex<ValueType>> exp_itheta(Nnds), exp_iktheta(Nnds);
            Vector<ValueType> Xsrc(Nnds*COORD_DIM);
            for (Long i = 0; i < Nnds; i++) {
              ValueType cos_i = cos(2*const_pi<ValueType>()*nds[i]);
              ValueType sin_i = sin(2*const_pi<ValueType>()*nds[i]);
              exp_itheta[i].real = cos_i;
              exp_itheta[i].imag = sin_i;
              exp_iktheta[i].real = 1;
              exp_iktheta[i].imag = 0;
              Xsrc[i*COORD_DIM+0] = cos_i;
              Xsrc[i*COORD_DIM+1] = sin_i;
              Xsrc[i*COORD_DIM+2] = 0;
            }

            Matrix<ValueType> Mker_sl, Mker_dl;
            GenericKernel<Laplace3D_FxU> laplace_sl;
            GenericKernel<Laplace3D_DxU> laplace_dl;
            laplace_sl.KernelMatrix(Mker_sl, Xtrg, Xsrc, Xsrc);
            laplace_dl.KernelMatrix(Mker_dl, Xtrg, Xsrc, Xsrc);
            SCTL_ASSERT(Mker_sl.Dim(0) == Nnds);
            SCTL_ASSERT(Mker_sl.Dim(1) == Ntrg);
            SCTL_ASSERT(Mker_dl.Dim(0) == Nnds);
            SCTL_ASSERT(Mker_dl.Dim(1) == Ntrg);

            Mintegrands.ReInit(Nnds, (Nmodes*2) * 2 * Ntrg);
            for (Long k = 0; k < Nmodes; k++) {
              for (Long i = 0; i < Nnds; i++) {
                for (Long j = 0; j < Ntrg; j++) {
                  Mintegrands[i][((k*2+0)*2+0)*Ntrg+j] = Mker_sl[i][j] * exp_iktheta[i].real;
                  Mintegrands[i][((k*2+1)*2+0)*Ntrg+j] = Mker_sl[i][j] * exp_iktheta[i].imag;
                  Mintegrands[i][((k*2+0)*2+1)*Ntrg+j] = Mker_dl[i][j] * exp_iktheta[i].real;
                  Mintegrands[i][((k*2+1)*2+1)*Ntrg+j] = Mker_dl[i][j] * exp_iktheta[i].imag;
                }
              }
              for (Long i = 0; i < Nnds; i++) {
                exp_iktheta[i] *= exp_itheta[i];
              }
            }
          };
          ValueType dist = 4*const_pi<ValueType>()*pow<ValueType,Long>(0.5,idx); // distance of target points from the source loop (which is a unit circle)
          discretize_basis_functions(Mintegrands, nds, wts, dist, LegendreQuadRule<ValueType>(45)); // TODO: adaptively select Legendre order

          Vector<ValueType> eps_vec;
          for (Long k = 0; k < max_digits; k++) eps_vec.PushBack(pow<ValueType,Long>(0.1,k));
          auto cond_num_vec = InterpQuadRule<ValueType>::Build(quad_nds, quad_wts,  Mintegrands, nds, wts, eps_vec);
        }
        for (Integer digits = 0; digits < max_digits; digits++) {
          Long N = quad_nds[digits].Dim();
          data[idx*max_digits+digits].ReInit(3*N);
          for (Long i = 0; i < N; i++) {
            data[idx*max_digits+digits][i*3+0] = cos<ValueType>(2*const_pi<ValueType>()*quad_nds[digits][i]);
            data[idx*max_digits+digits][i*3+1] = sin<ValueType>(2*const_pi<ValueType>()*quad_nds[digits][i]);
            data[idx*max_digits+digits][i*3+2] = (2*const_pi<ValueType>()*quad_wts[digits][i]);
          }
        }
      }
      WriteFile(data, fname);
    }
    for (Integer idx = 0; idx < crossover_adap_depth; idx++) { // Use trapezoidal rule up to crossover_adap_depth
      for (Integer digits = 0; digits < max_digits; digits++) {
        Long N = std::max<Long>(digits*pow<Long,Long>(2,idx), Nmodes); // TODO: determine optimal order by testing error or adaptively
        data[idx*max_digits+digits].ReInit(3*N);
        for (Long i = 0; i < N; i++) {
          ValueType quad_nds = i/(ValueType)N;
          ValueType quad_wts = 1/(ValueType)N;
          data[idx*max_digits+digits][i*3+0] = cos<ValueType>(2*const_pi<ValueType>()*quad_nds);
          data[idx*max_digits+digits][i*3+1] = sin<ValueType>(2*const_pi<ValueType>()*quad_nds);
          data[idx*max_digits+digits][i*3+2] = (2*const_pi<ValueType>()*quad_wts);
        }
      }
    }

    Vector<Vector<RealType>> quad_rule_lst;
    quad_rule_lst.ReInit(data.Dim()*3);
    for (Integer i = 0; i < data.Dim(); i++) {
      uint64_t data_len = data[i].Dim()/3;
      quad_rule_lst[i*3+0].ReInit(data_len);
      quad_rule_lst[i*3+1].ReInit(data_len);
      quad_rule_lst[i*3+2].ReInit(data_len);
      for (Long j = 0; j < (Long)data_len; j++) {
        quad_rule_lst[i*3+0][j] = (RealType)data[i][j*3+0];
        quad_rule_lst[i*3+1][j] = (RealType)data[i][j*3+1];
        quad_rule_lst[i*3+2][j] = (RealType)data[i][j*3+2];
      }
    }
    return quad_rule_lst;
  }
  template <class RealType> static Complex<RealType> ToroidalSpecialQuadRule(Matrix<RealType>& Mfourier, Vector<Complex<RealType>>& nds, Vector<RealType>& wts, const Integer Nmodes, const Tensor<RealType,true,3,1>& Xt_X0, const Tensor<RealType,true,3,1>& e1, const Tensor<RealType,true,3,1>& e2, const Tensor<RealType,true,3,1>& e1xe2, RealType R0, Integer digits) {
    constexpr Integer max_adap_depth = 30; // build quadrature rules for points up to 2*pi*0.5^max_adap_depth from source loop
    constexpr Integer crossover_adap_depth = 3;
    constexpr Integer max_digits = 20;
    SCTL_ASSERT(digits<max_digits);

    const RealType XX = dot_prod(Xt_X0, e1);
    const RealType YY = dot_prod(Xt_X0, e2);
    const RealType ZZ = dot_prod(Xt_X0, e1xe2);
    const RealType R = sqrt<RealType>(XX*XX+YY*YY);
    const RealType Rinv = 1/R;
    const RealType dtheta = sqrt<RealType>((R-R0)*(R-R0) + ZZ*ZZ)/R0;
    const Complex<RealType> exp_theta0(XX*Rinv, YY*Rinv);
    Long adap_depth = 0;
    { // Set adap_depth
      for (RealType s = dtheta; s<2*const_pi<RealType>(); s*=2) adap_depth++;
      SCTL_ASSERT(adap_depth < max_adap_depth);
    }

    SCTL_ASSERT(Nmodes < 100);
    static Vector<Vector<Matrix<RealType>>> all_fourier_basis(100);
    static Vector<Vector<Vector<Complex<RealType>>>> all_quad_nds(100);
    static Vector<Vector<Vector<RealType>>> all_quad_wts(100);
    #pragma omp critical
    if (all_quad_nds[Nmodes].Dim() == 0) {
      auto quad_rules = BuildToroidalSpecialQuadRules<RealType>(Nmodes);
      const Long Nrules = quad_rules.Dim()/3;

      Vector<Matrix<RealType>> fourier_basis(Nrules);
      Vector<Vector<Complex<RealType>>> quad_nds(Nrules);
      Vector<Vector<RealType>> quad_wts(Nrules);
      for (Long i = 0; i < Nrules; i++) { // Set quad_nds, quad_wts, fourier_basis
        const Integer Nnds = quad_rules[i*3+0].Dim();
        Vector<Complex<RealType>> exp_itheta(Nnds);
        Vector<Complex<RealType>> exp_iktheta(Nnds);
        for (Integer j = 0; j < Nnds; j++) {
          exp_itheta[j].real = quad_rules[i*3+0][j];
          exp_itheta[j].imag = quad_rules[i*3+1][j];
          exp_iktheta[j].real = 1;
          exp_iktheta[j].imag = 0;
        }
        quad_wts[i] = quad_rules[i*3+2];
        quad_nds[i] = exp_itheta;

        auto& Mexp_iktheta = fourier_basis[i];
        Mexp_iktheta.ReInit(Nmodes*2, Nnds);
        for (Integer k = 0; k < Nmodes; k++) {
          for (Integer j = 0; j < Nnds; j++) {
            Mexp_iktheta[k*2+0][j] = exp_iktheta[j].real;
            Mexp_iktheta[k*2+1][j] = exp_iktheta[j].imag;
          }
          exp_iktheta *= exp_itheta;
        }
      }
      all_fourier_basis[Nmodes].Swap(fourier_basis);
      all_quad_wts[Nmodes].Swap(quad_wts);
      all_quad_nds[Nmodes].Swap(quad_nds);
    }

    { // Set Mfourier, nds, wts
      const Long quad_idx = adap_depth*max_digits+digits;
      const auto& Mfourier0 = all_fourier_basis[Nmodes][quad_idx];
      const auto& nds0 = all_quad_nds[Nmodes][quad_idx];
      const auto& wts0 = all_quad_wts[Nmodes][quad_idx];
      const Long N = wts0.Dim();

      Mfourier.ReInit(Mfourier0.Dim(0), Mfourier0.Dim(1), (Iterator<RealType>)Mfourier0.begin(), false);
      nds.ReInit(N, (Iterator<Complex<RealType>>)nds0.begin(), false);
      wts.ReInit(N, (Iterator<RealType>)wts0.begin(), false);

      if (adap_depth >= crossover_adap_depth) return exp_theta0;
      else return Complex<RealType>(1,0);
    }
  }
  template <class RealType, class Kernel> static void toroidal_greens_fn_batched(Matrix<RealType>& M, const Tensor<RealType,true,3,1>& y_trg, const Matrix<RealType>& x_src, const Matrix<RealType>& dx_src, const Matrix<RealType>& d2x_src, const Matrix<RealType>& r_src, const Matrix<RealType>& dr_src, const Matrix<RealType>& e1_src, const Matrix<RealType>& e2_src, const Matrix<RealType>& de1_src, const Matrix<RealType>& de2_src, const Kernel& ker, const Integer FourierModes, const Integer digits) {
    constexpr Integer KDIM0 = Kernel::SrcDim();
    constexpr Integer KDIM1 = Kernel::TrgDim();
    constexpr Integer Nbuff = 10000; // TODO

    constexpr Integer COORD_DIM = 3;
    using Vec3 = Tensor<RealType,true,COORD_DIM,1>;

    const Long BatchSize = M.Dim(0);
    SCTL_ASSERT(M.Dim(1) == FourierModes*2*KDIM0 * KDIM1);
    SCTL_ASSERT(  x_src.Dim(1) == BatchSize &&   x_src.Dim(0) == COORD_DIM);
    SCTL_ASSERT( dx_src.Dim(1) == BatchSize &&  dx_src.Dim(0) == COORD_DIM);
    SCTL_ASSERT(d2x_src.Dim(1) == BatchSize && d2x_src.Dim(0) == COORD_DIM);
    SCTL_ASSERT(  r_src.Dim(1) == BatchSize &&   r_src.Dim(0) ==         1);
    SCTL_ASSERT( dr_src.Dim(1) == BatchSize &&  dr_src.Dim(0) ==         1);
    SCTL_ASSERT( e1_src.Dim(1) == BatchSize &&  e1_src.Dim(0) == COORD_DIM);
    SCTL_ASSERT( e2_src.Dim(1) == BatchSize &&  e2_src.Dim(0) == COORD_DIM);
    SCTL_ASSERT(de1_src.Dim(1) == BatchSize && de1_src.Dim(0) == COORD_DIM);
    SCTL_ASSERT(de2_src.Dim(1) == BatchSize && de2_src.Dim(0) == COORD_DIM);
    for (Long ii = 0; ii < BatchSize; ii++) {
      RealType r = r_src[0][ii], dr = dr_src[0][ii];
      Vec3 x, dx, d2x, e1, e2, de1, de2;
      { // Set x, dx, d2x, e1, e2, de1, de2
        for (Integer k = 0; k < COORD_DIM; k++) {
          x  (k,0) =   x_src[k][ii];
          dx (k,0) =  dx_src[k][ii];
          d2x(k,0) = d2x_src[k][ii];
          e1 (k,0) =  e1_src[k][ii];
          e2 (k,0) =  e2_src[k][ii];
          de1(k,0) = de1_src[k][ii];
          de2(k,0) = de2_src[k][ii];
        }
      }

      auto toroidal_greens_fn = [&ker,&FourierModes,&digits](Matrix<RealType>& M, const Vec3& Xt, const Vec3& x, const Vec3& dx, const Vec3& d2x, const Vec3& e1, const Vec3& e2, const Vec3& de1, const Vec3& de2, const RealType r, const RealType dr) {
        SCTL_ASSERT(M.Dim(0) == FourierModes*2*KDIM0);
        SCTL_ASSERT(M.Dim(1) ==                KDIM1);

        Matrix<RealType> Mexp_iktheta;
        Vector<Complex<RealType>> nds;
        Vector<RealType> wts;
        const auto exp_theta = ToroidalSpecialQuadRule<RealType>(Mexp_iktheta, nds, wts, FourierModes+1, Xt-x, e1, e2, cross_prod(e1,e2), r, digits);
        const Long Nnds = wts.Dim();
        SCTL_ASSERT(Nnds < Nbuff);

        StaticArray<RealType,(COORD_DIM*2+1)*Nbuff> mem_buff1;
        Vector<RealType> y(Nnds*COORD_DIM, mem_buff1+0*COORD_DIM*Nbuff, false);
        Vector<RealType> n(Nnds*COORD_DIM, mem_buff1+1*COORD_DIM*Nbuff, false);
        Vector<RealType> da(         Nnds, mem_buff1+2*COORD_DIM*Nbuff, false);
        for (Integer j = 0; j < Nnds; j++) { // Set x, n, da
          auto nds_ = nds[j] * exp_theta; // rotate
          RealType cost = nds_.real;
          RealType sint = nds_.imag;

          Vec3 dy_ds = dx + e1*(dr*cost) + e2*(dr*sint) + de1*(r*cost) + de2*(r*sint);
          Vec3 dy_dt = e1*(-r*sint) + e2*(r*cost);

          Vec3 y_ = x + e1*(r*cost) + e2*(r*sint);
          Vec3 n_ = cross_prod(dy_ds, dy_dt);
          RealType da_ = sqrt<RealType>(dot_prod(n_,n_));
          n_ = n_ * (1/da_);

          for (Integer k = 0; k < COORD_DIM; k++) {
            y[j*COORD_DIM+k] = y_(k,0);
            n[j*COORD_DIM+k] = n_(k,0);
          }
          da[j] = da_;
        }

        StaticArray<RealType,KDIM0*KDIM1*Nbuff> mem_buff2;
        Matrix<RealType> Mker(Nnds*KDIM0, KDIM1, mem_buff2, false);
        ker.KernelMatrix(Mker, Vector<RealType>(COORD_DIM,(Iterator<RealType>)Xt.begin(),false), y, n);

        { // Set M <-- Mexp_iktheta * diag(wts*da) * Mker
          Matrix<RealType> Mker_da(Nnds, KDIM0*KDIM1, Mker.begin(), false);
          for (Integer j = 0; j < Nnds; j++) {
            for (Integer k = 0; k < KDIM0*KDIM1; k++) {
              Mker_da[j][k] *= da[j] * wts[j];
            }
          }

          const Matrix<RealType> Mexp_iktheta_(FourierModes*2, Nnds, Mexp_iktheta.begin(), false);
          Matrix<RealType> M_(FourierModes*2, KDIM0*KDIM1, M.begin(), false);
          Matrix<RealType>::GEMM(M_, Mexp_iktheta_, Mker_da);

          Complex<RealType> exp_iktheta(1,0);
          for (Integer j = 0; j < FourierModes; j++) {
            for (Integer k = 0; k < KDIM0*KDIM1; k++) {
              Complex<RealType> Mjk(M_[j*2+0][k],M_[j*2+1][k]);
              Mjk *= exp_iktheta;
              M_[j*2+0][k] = Mjk.real;
              M_[j*2+1][k] = Mjk.imag;
            }
            exp_iktheta *= exp_theta;
          }
        }
      };
      Matrix<RealType> M_toroidal_greens_fn(FourierModes*2*KDIM0, KDIM1, M[ii], false);
      toroidal_greens_fn(M_toroidal_greens_fn, y_trg, x, dx, d2x, e1, e2, de1, de2, r, dr);
    }
  }

  template <class ValueType, class Kernel> static void SpecialQuadBuildBasisMatrix(Matrix<ValueType>& M, Vector<ValueType>& quad_nds, Vector<ValueType>& quad_wts, const Integer Ncheb, const Integer FourierModes, const Integer max_digits, const ValueType elem_length, const Integer RefLevels, const Kernel& ker) {
    // TODO: cleanup
    constexpr Integer COORD_DIM = 3;
    using Vec3 = Tensor<ValueType,true,COORD_DIM,1>;

    const Long LegQuadOrder = 2*max_digits;
    constexpr Long LogQuadOrder = 18; // this has non-negative weights

    constexpr Integer KDIM0 = Kernel::SrcDim();
    constexpr Integer KDIM1 = Kernel::TrgDim();

    Vec3 y_trg;
    y_trg(0,0) = 1;
    y_trg(1,0) = 0;
    y_trg(2,0) = 0;

    Vector<ValueType> radius(          Ncheb);
    Vector<ValueType> coord (COORD_DIM*Ncheb);
    Vector<ValueType> dr    (          Ncheb);
    Vector<ValueType> dx    (COORD_DIM*Ncheb);
    Vector<ValueType> d2x   (COORD_DIM*Ncheb);
    Vector<ValueType> e1    (COORD_DIM*Ncheb);
    for (Long i = 0; i < Ncheb; i++) {
      radius[i] = 1;
      dr[i] = 0;

      coord[0*Ncheb+i] = 0;
      coord[1*Ncheb+i] = 0;
      coord[2*Ncheb+i] = ChebQuadRule<ValueType>::nds(Ncheb)[i] * elem_length;

      dx[0*Ncheb+i] = 0;
      dx[1*Ncheb+i] = 0;
      dx[2*Ncheb+i] = elem_length;

      d2x[0*Ncheb+i] = 0;
      d2x[1*Ncheb+i] = 0;
      d2x[2*Ncheb+i] = 0;

      e1[0*Ncheb+i] = 1;
      e1[1*Ncheb+i] = 0;
      e1[2*Ncheb+i] = 0;
    }

    auto adap_ref = [&LegQuadOrder](Vector<ValueType>& nds, Vector<ValueType>& wts, ValueType a, ValueType b, Integer levels) {
      const auto& log_quad_nds = LogSingularityQuadRule<ValueType>(LogQuadOrder).first;
      const auto& log_quad_wts = LogSingularityQuadRule<ValueType>(LogQuadOrder).second;
      const auto& leg_nds = LegendreQuadRule<ValueType>(LegQuadOrder).first;
      const auto& leg_wts = LegendreQuadRule<ValueType>(LegQuadOrder).second;

      Long N = std::max<Integer>(levels-1,0)*LegQuadOrder + LogQuadOrder;
      if (nds.Dim() != N) nds.ReInit(N);
      if (wts.Dim() != N) wts.ReInit(N);

      Vector<ValueType> nds_(nds.Dim(), nds.begin(), false);
      Vector<ValueType> wts_(wts.Dim(), wts.begin(), false);
      while (levels>1) {
        Vector<ValueType> nds0(LegQuadOrder, nds_.begin(), false);
        Vector<ValueType> wts0(LegQuadOrder, wts_.begin(), false);
        Vector<ValueType> nds1(nds_.Dim()-LegQuadOrder, nds_.begin()+LegQuadOrder, false);
        Vector<ValueType> wts1(wts_.Dim()-LegQuadOrder, wts_.begin()+LegQuadOrder, false);

        ValueType end_point = (a+b)/2;
        nds0 = leg_nds * (a-end_point) + end_point;
        wts0 = leg_wts * fabs<ValueType>(a-end_point);

        nds_.Swap(nds1);
        wts_.Swap(wts1);
        a = end_point;
        levels--;
      }
      nds_ = log_quad_nds * (a-b) + b;
      wts_ = log_quad_wts * fabs<ValueType>(a-b);
    };
    adap_ref(quad_nds, quad_wts, (ValueType)1, (ValueType)0, RefLevels); // adaptive quadrature rule

    Matrix<ValueType> Minterp_quad_nds;
    { // Set Minterp_quad_nds
      Minterp_quad_nds.ReInit(Ncheb, quad_nds.Dim());
      Vector<ValueType> Vinterp_quad_nds(Ncheb*quad_nds.Dim(), Minterp_quad_nds.begin(), false);
      LagrangeInterp<ValueType>::Interpolate(Vinterp_quad_nds, ChebQuadRule<ValueType>::nds(Ncheb), quad_nds);
    }

    Matrix<ValueType> r_src, dr_src, x_src, dx_src, d2x_src, e1_src, e2_src, de1_src, de2_src;
    r_src  .ReInit(        1,quad_nds.Dim());
    dr_src .ReInit(        1,quad_nds.Dim());
    x_src  .ReInit(COORD_DIM,quad_nds.Dim());
    dx_src .ReInit(COORD_DIM,quad_nds.Dim());
    d2x_src.ReInit(COORD_DIM,quad_nds.Dim());
    e1_src .ReInit(COORD_DIM,quad_nds.Dim());
    e2_src .ReInit(COORD_DIM,quad_nds.Dim());
    de1_src.ReInit(COORD_DIM,quad_nds.Dim());
    de2_src.ReInit(COORD_DIM,quad_nds.Dim());
    Matrix<ValueType>::GEMM(  x_src, Matrix<ValueType>(COORD_DIM,Ncheb, coord.begin(),false), Minterp_quad_nds);
    Matrix<ValueType>::GEMM( dx_src, Matrix<ValueType>(COORD_DIM,Ncheb,    dx.begin(),false), Minterp_quad_nds);
    Matrix<ValueType>::GEMM(d2x_src, Matrix<ValueType>(COORD_DIM,Ncheb,   d2x.begin(),false), Minterp_quad_nds);
    Matrix<ValueType>::GEMM(  r_src, Matrix<ValueType>(        1,Ncheb,radius.begin(),false), Minterp_quad_nds);
    Matrix<ValueType>::GEMM( dr_src, Matrix<ValueType>(        1,Ncheb,    dr.begin(),false), Minterp_quad_nds);
    Matrix<ValueType>::GEMM( e1_src, Matrix<ValueType>(COORD_DIM,Ncheb,    e1.begin(),false), Minterp_quad_nds);
    for (Long j = 0; j < quad_nds.Dim(); j++) { // Set e2_src
      Vec3 e1, dx, d2x;
      for (Integer k = 0; k < COORD_DIM; k++) {
        e1(k,0) = e1_src[k][j];
        dx(k,0) = dx_src[k][j];
        d2x(k,0) = d2x_src[k][j];
      }
      ValueType inv_dx2 = 1/dot_prod(dx,dx);
      e1 = e1 - dx * dot_prod(e1, dx) * inv_dx2;
      e1 = e1 * (1/sqrt<ValueType>(dot_prod(e1,e1)));

      Vec3 e2 = cross_prod(e1, dx);
      e2 = e2 * (1/sqrt<ValueType>(dot_prod(e2,e2)));
      Vec3 de1 = dx*(-dot_prod(e1,d2x) * inv_dx2);
      Vec3 de2 = dx*(-dot_prod(e2,d2x) * inv_dx2);
      for (Integer k = 0; k < COORD_DIM; k++) {
        e1_src[k][j] = e1(k,0);
        e2_src[k][j] = e2(k,0);
        de1_src[k][j] = de1(k,0);
        de2_src[k][j] = de2(k,0);
      }
    }

    Matrix<ValueType> M_tor(quad_nds.Dim(), FourierModes*2*KDIM0*KDIM1);
    toroidal_greens_fn_batched(M_tor, y_trg, x_src, dx_src, d2x_src, r_src, dr_src, e1_src, e2_src, de1_src, de2_src, ker, FourierModes, max_digits);

    M.ReInit(quad_nds.Dim(), Ncheb*FourierModes*2*KDIM0*KDIM1);
    for (Long i = 0; i < quad_nds.Dim(); i++) {
      for (Long j = 0; j < Ncheb; j++) {
        for (Long k = 0; k < FourierModes*2*KDIM0*KDIM1; k++) {
          M[i][j*FourierModes*2*KDIM0*KDIM1+k] = Minterp_quad_nds[j][i] * M_tor[i][k];
        }
      }
    }
  }
  template <class ValueType> static Vector<Vector<ValueType>> BuildSpecialQuadRules(const Integer Ncheb, const Integer FourierModes, const ValueType elem_length) {
    constexpr Integer Nlen = 10; // number of length samples in [elem_length/sqrt(2), elem_length*sqrt(2)]
    constexpr Integer max_digits = 19;
    Integer depth = (Integer)(log<ValueType>(elem_length)/log<ValueType>(2)+4);

    GenericKernel<Laplace3D_FxU> laplace_sl; // TODO
    GenericKernel<Laplace3D_DxU> laplace_dl; // TODO

    Vector<ValueType> nds, wts;
    Matrix<ValueType> Mintegrands;
    { // Set nds, wts, Mintegrands
      Vector<Matrix<ValueType>> Msl(Nlen), Mdl(Nlen);
      Vector<Vector<ValueType>> nds_(Nlen), wts_(Nlen);
      #pragma omp parallel for schedule(static)
      for (Long k = 0; k < Nlen; k++) {
        ValueType length = elem_length/sqrt<ValueType>(2.0)*k/(Nlen-1) + elem_length*sqrt<ValueType>(2.0)*(Nlen-k-1)/(Nlen-1);
        SpecialQuadBuildBasisMatrix(Msl[k], nds_[k], wts_[k], Ncheb,FourierModes, max_digits, length, depth, laplace_sl);
        SpecialQuadBuildBasisMatrix(Mdl[k], nds_[k], wts_[k], Ncheb,FourierModes, max_digits, length, depth, laplace_dl);
      }
      nds = nds_[0];
      wts = wts_[0];

      const Long N0 = nds.Dim();
      Vector<Long> cnt(Nlen*2), dsp(Nlen*2); dsp[0] = 0;
      for (Long k = 0; k < Nlen; k++) {
        SCTL_ASSERT(Msl[k].Dim(0) == N0);
        SCTL_ASSERT(Mdl[k].Dim(0) == N0);
        cnt[k*2+0] = Msl[k].Dim(1);
        cnt[k*2+1] = Mdl[k].Dim(1);
      }
      omp_par::scan(cnt.begin(), dsp.begin(), cnt.Dim());

      Mintegrands.ReInit(N0, dsp[Nlen*2-1] + cnt[Nlen*2-1]);
      #pragma omp parallel for schedule(static)
      for (Long k = 0; k < Nlen; k++) {
        for (Long i = 0; i < N0; i++) {
          for (Long j = 0; j < cnt[k*2+0]; j++) {
            Mintegrands[i][dsp[k*2+0]+j] = Msl[k][i][j];
          }
        }
        for (Long i = 0; i < N0; i++) {
          for (Long j = 0; j < cnt[k*2+1]; j++) {
            Mintegrands[i][dsp[k*2+1]+j] = Mdl[k][i][j];
          }
        }
      }
    }

    Vector<Vector<ValueType>> nds_wts(max_digits*2);
    { // Set nds_wts
      Vector<ValueType> eps_vec;
      Vector<Vector<ValueType>> quad_nds, quad_wts;
      for (Long k = 0; k < max_digits; k++) eps_vec.PushBack(pow<ValueType,Long>(0.1,k));
      InterpQuadRule<ValueType>::Build(quad_nds, quad_wts,  Mintegrands, nds, wts, eps_vec);
      SCTL_ASSERT(quad_nds.Dim() == max_digits);
      SCTL_ASSERT(quad_wts.Dim() == max_digits);
      for (Long k = 0; k < max_digits; k++) {
        nds_wts[k*2+0] = quad_nds[k];
        nds_wts[k*2+1] = quad_wts[k];
      }
    }
    return nds_wts;
  }
  template <class Real, bool adap_quad=false> static void SpecialQuadRule(Vector<Real>& nds, Vector<Real>& wts, const Integer ChebOrder, const Real s, const Real elem_radius, const Real elem_length, const Integer digits) {
    constexpr Integer max_adap_depth = 15; // TODO
    constexpr Integer MaxFourierModes = 8; // TODO
    constexpr Integer MaxChebOrder = 100;
    constexpr Integer max_digits = 19;

    auto LogSingularQuadOrder = [](Integer digits) { return 2*digits; }; // TODO: determine optimal order
    auto LegQuadOrder = [](Integer digits) { return 2*digits; }; // TODO: determine optimal order

    auto one_sided_rule = [&ChebOrder,&LogSingularQuadOrder,&LegQuadOrder,&digits](Real radius, Real length) -> std::pair<Vector<Real>,Vector<Real>> {
      auto load_special_quad_rule = [](const Integer ChebOrder){
        const std::string fname = std::string(("special_quad_q")+std::to_string(ChebOrder));
        using ValueType = QuadReal;

        Vector<Vector<ValueType>> data;
        ReadFile(data, fname);
        if (data.Dim() != max_adap_depth*max_digits*2) { // build quadrature rules
          data.ReInit(max_adap_depth*max_digits*2);
          ValueType length = 960.0; // TODO
          for (Integer i = 0; i < max_adap_depth; i++) {
            auto nds_wts = BuildSpecialQuadRules<ValueType>(ChebOrder, MaxFourierModes, length);
            for (Long j = 0; j < max_digits; j++) {
              data[(i*max_digits+j)*2+0] = nds_wts[j*2+0];
              data[(i*max_digits+j)*2+1] = nds_wts[j*2+1];
            }
            length *= (ValueType)0.5;
          }
          WriteFile(data, fname);
        }

        Vector<std::pair<Vector<Real>,Vector<Real>>> nds_wts_lst(max_adap_depth*max_digits);
        for (Long i = 0; i < max_adap_depth*max_digits; i++) { // Set nds_wts_lst
          const auto& nds_ = data[i*2+0];
          const auto& wts_ = data[i*2+1];
          nds_wts_lst[i]. first.ReInit(nds_.Dim());
          nds_wts_lst[i].second.ReInit(wts_.Dim());
          for (Long j = 0; j < nds_.Dim(); j++) {
            nds_wts_lst[i]. first[j] = (Real)nds_[j];
            nds_wts_lst[i].second[j] = (Real)wts_[j];
          }
        }
        return nds_wts_lst;
      };
      const auto& log_sing_nds_wts = LogSingularityQuadRule<Real>(LogSingularQuadOrder(digits));
      const auto& leg_nds_wts = LegendreQuadRule<Real>(LegQuadOrder(digits));
      const auto& leg_nds = leg_nds_wts.first;
      const auto& leg_wts = leg_nds_wts.second;

      std::pair<Vector<Real>,Vector<Real>> nds_wts;
      if (adap_quad) {
        if (length < 0.8*radius) { // log-singular quadrature
          nds_wts = log_sing_nds_wts;
        } else { // adaptive with scale-dependent quadrature
          Real s = 1.0;
          while (length*s>0.8*radius) {
            s*=0.5;
            for (Long i = 0; i < leg_nds.Dim(); i++) {
              nds_wts.first.PushBack(leg_nds[i]*s+s);
              nds_wts.second.PushBack(leg_wts[i]*s);
            }
          }
          { // add rule for singular part
            const auto& sing_nds = log_sing_nds_wts.first;
            const auto& sing_wts = log_sing_nds_wts.second;
            for (Long i = 0; i < sing_nds.Dim(); i++) {
              nds_wts.first.PushBack(sing_nds[i]*s);
              nds_wts.second.PushBack(sing_wts[i]*s);
            }
          }
        }
      } else {
        static Vector<Vector<std::pair<Vector<Real>,Vector<Real>>>> nds_wts_lst(MaxChebOrder);
        SCTL_ASSERT(ChebOrder < MaxChebOrder);
        #pragma omp critical
        if (!nds_wts_lst[ChebOrder].Dim()) {
          auto nds_wts_lst0 = load_special_quad_rule(ChebOrder);
          nds_wts_lst[ChebOrder].Swap(nds_wts_lst0);
        }
        if (length < 0.8*radius) { // log-singular quadrature
          nds_wts = log_sing_nds_wts;
        } else if (length < 1280*radius) { // scale-dependent quadrature
          Long quad_idx = 0;
          { // Set quad_idx
            Real min_dist = 1e10;
            Real l_over_r = 1280;
            Real length_over_radius = length/radius;
            for (Integer i = 0; i < max_adap_depth; i++) {
              if (min_dist > fabs(l_over_r - length_over_radius)) {
                min_dist = fabs(l_over_r - length_over_radius);
                quad_idx = i;
              }
              l_over_r *= 0.5;
            }
          }
          nds_wts = nds_wts_lst[ChebOrder][quad_idx*max_digits+digits];
        } else { // adaptive with scale-dependent quadrature
          Real s = 1.0;
          while (length*s>1280*radius) {
            s*=0.5;
            for (Long i = 0; i < leg_nds.Dim(); i++) {
              nds_wts.first.PushBack(leg_nds[i]*s+s);
              nds_wts.second.PushBack(leg_wts[i]*s);
            }
          }
          { // add rule for singular part
            const auto& sing_nds = nds_wts_lst[ChebOrder][0*max_digits+digits].first;
            const auto& sing_wts = nds_wts_lst[ChebOrder][0*max_digits+digits].second;
            for (Long i = 0; i < sing_nds.Dim(); i++) {
              nds_wts.first.PushBack(sing_nds[i]*s);
              nds_wts.second.PushBack(sing_wts[i]*s);
            }
          }
        }
      }
      return nds_wts;
    };
    const auto& nds_wts0 = one_sided_rule(elem_radius, elem_length*s);
    const auto& nds_wts1 = one_sided_rule(elem_radius, elem_length*(1-s));
    const Long N0 = nds_wts0.first.Dim();
    const Long N1 = nds_wts1.first.Dim();

    nds.ReInit(N0 + N1);
    wts.ReInit(N0 + N1);
    Vector<Real> nds0(N0, nds.begin() + 0*N0, false);
    Vector<Real> wts0(N0, wts.begin() + 0*N0, false);
    Vector<Real> nds1(N1, nds.begin() + 1*N0, false);
    Vector<Real> wts1(N1, wts.begin() + 1*N0, false);
    nds0 = (nds_wts0.first*(-1)+1)*s;
    wts0 = (nds_wts0.second      )*s;
    nds1 = (nds_wts1.first )*(1-s)+s;
    wts1 = (nds_wts1.second)*(1-s);
  }




  template <class Real> SlenderElemList<Real>::SlenderElemList(const Vector<Long>& cheb_order0, const Vector<Long>& fourier_order0, const Vector<Real>& coord0, const Vector<Real>& radius0, const Vector<Real>& orientation0) {
    Init(cheb_order0, fourier_order0, coord0, radius0, orientation0);
  }
  template <class Real> void SlenderElemList<Real>::Init(const Vector<Long>& cheb_order0, const Vector<Long>& fourier_order0, const Vector<Real>& coord0, const Vector<Real>& radius0, const Vector<Real>& orientation0) {
    using Vec3 = Tensor<Real,true,COORD_DIM,1>;
    const Long Nelem = cheb_order0.Dim();
    SCTL_ASSERT(fourier_order0.Dim() == Nelem);

    cheb_order = cheb_order0;
    fourier_order = fourier_order0;
    elem_dsp.ReInit(Nelem); elem_dsp[0] = 0;
    omp_par::scan(cheb_order.begin(), elem_dsp.begin(), Nelem);

    const Long Nnodes = (Nelem ? cheb_order[Nelem-1]+elem_dsp[Nelem-1] : 0);
    SCTL_ASSERT_MSG(coord0.Dim() == Nnodes * COORD_DIM, "Length of the coordinate vector does not match the number of nodes.");
    SCTL_ASSERT_MSG(radius0.Dim() == Nnodes, "Length of the radius vector does not match the number of nodes.");

    radius = radius0;
    coord.ReInit(COORD_DIM*Nnodes);
    e1   .ReInit(COORD_DIM*Nnodes);
    dr   .ReInit(          Nnodes);
    dx   .ReInit(COORD_DIM*Nnodes);
    d2x  .ReInit(COORD_DIM*Nnodes);
    for (Long i = 0; i < Nelem; i++) { // Set coord, radius, dr, ds, d2s
      const Long Ncheb = cheb_order[i];
      Vector<Real> radius_(          Ncheb, radius.begin()+          elem_dsp[i], false);
      Vector<Real>  coord_(COORD_DIM*Ncheb,  coord.begin()+COORD_DIM*elem_dsp[i], false);
      Vector<Real>     e1_(COORD_DIM*Ncheb,     e1.begin()+COORD_DIM*elem_dsp[i], false);
      Vector<Real>     dr_(          Ncheb,     dr.begin()+          elem_dsp[i], false);
      Vector<Real>     dx_(COORD_DIM*Ncheb,     dx.begin()+COORD_DIM*elem_dsp[i], false);
      Vector<Real>    d2x_(COORD_DIM*Ncheb,    d2x.begin()+COORD_DIM*elem_dsp[i], false);

      const Vector<Real> coord__(COORD_DIM*Ncheb, (Iterator<Real>)coord0.begin()+elem_dsp[i]*COORD_DIM, false);
      for (Long j = 0; j < Ncheb; j++) { // Set coord_
        for (Long k = 0; k < COORD_DIM; k++) {
          coord_[k*Ncheb+j] = coord__[j*COORD_DIM+k];
        }
      }

      LagrangeInterp<Real>::Derivative( dr_, radius_, CenterlineNodes(Ncheb));
      LagrangeInterp<Real>::Derivative( dx_,  coord_, CenterlineNodes(Ncheb));
      LagrangeInterp<Real>::Derivative(d2x_,     dx_, CenterlineNodes(Ncheb));
      if (orientation0.Dim()) { // Set e1_
        SCTL_ASSERT(orientation0.Dim() == Nnodes*COORD_DIM);
        const Vector<Real> orientation__(COORD_DIM*Ncheb, (Iterator<Real>)orientation0.begin()+elem_dsp[i]*COORD_DIM, false);
        for (Long j = 0; j < Ncheb; j++) {
          for (Integer k = 0; k < COORD_DIM; k++) {
            e1_[k*Ncheb+j] = orientation__[j*COORD_DIM+k];
          }
        }
      } else {
        Integer orient_dir = 0;
        for (Integer k = 0; k < COORD_DIM; k++) {
          e1_[k*Ncheb+0] = 0;
          if (fabs(dx_[k*Ncheb+0]) < fabs(dx_[orient_dir*Ncheb+0])) orient_dir = k;
        }
        e1_[orient_dir*Ncheb+0] = 1;
        for (Long j = 0; j < Ncheb; j++) {
          Vec3 e1_vec, dx_vec;
          for (Integer k = 0; k < COORD_DIM; k++) {
            e1_vec(k,0) = (j==0 ? e1_[k*Ncheb] : e1_[k*Ncheb+j-1]);
            dx_vec(k,0) = dx_[k*Ncheb+j];
          }
          e1_vec = e1_vec - dx_vec*(dot_prod(dx_vec,e1_vec)/dot_prod(dx_vec,dx_vec));
          Real scal = (1.0/sqrt<Real>(dot_prod(e1_vec,e1_vec)));
          for (Integer k = 0; k < COORD_DIM; k++) {
            e1_[k*Ncheb+j] = e1_vec(k,0) * scal;
          }
        }
      }
    }
  }

  template <class Real> void SlenderElemList<Real>::GetNodeCoord(Vector<Real>* X, Vector<Real>* Xn, Vector<Long>* element_wise_node_cnt) {
    const Long Nelem = cheb_order.Dim();
    Vector<Long> node_cnt(Nelem), node_dsp(Nelem);
    { // Set node_cnt, node_dsp
      for (Long i = 0; i < Nelem; i++) {
        node_cnt[i] = cheb_order[i] * fourier_order[i];
      }
      if (Nelem) node_dsp[0] = 0;
      omp_par::scan(node_cnt.begin(), node_dsp.begin(), Nelem);
    }

    const Long Nnodes = (Nelem ? node_dsp[Nelem-1]+node_cnt[Nelem-1] : 0);
    if (element_wise_node_cnt) (*element_wise_node_cnt) = node_cnt;
    if (X  != nullptr && X ->Dim() != Nnodes*COORD_DIM) X ->ReInit(Nnodes*COORD_DIM);
    if (Xn != nullptr && Xn->Dim() != Nnodes*COORD_DIM) Xn->ReInit(Nnodes*COORD_DIM);
    for (Long i = 0; i < Nelem; i++) {
      Vector<Real> X_, Xn_;
      if (X  != nullptr) X_ .ReInit(node_cnt[i]*COORD_DIM, X ->begin()+node_dsp[i]*COORD_DIM, false);
      if (Xn != nullptr) Xn_.ReInit(node_cnt[i]*COORD_DIM, Xn->begin()+node_dsp[i]*COORD_DIM, false);
      GetGeom((X==nullptr?nullptr:&X_), (Xn==nullptr?nullptr:&Xn_), nullptr,nullptr,nullptr, CenterlineNodes(cheb_order[i]), sin_theta<Real>(fourier_order[i]), cos_theta<Real>(fourier_order[i]), i);
    }
  }
  template <class Real> void SlenderElemList<Real>::GetFarFieldNodes(Vector<Real>& X, Vector<Real>& Xn, Vector<Real>& wts, Vector<Real>& dist_far, Vector<Long>& element_wise_node_cnt, const Real tol) {
    const Long Nelem = cheb_order.Dim();
    Vector<Long> node_cnt(Nelem), node_dsp(Nelem);
    { // Set node_cnt, node_dsp
      for (Long i = 0; i < Nelem; i++) {
        node_cnt[i] = cheb_order[i]*FARFIELD_UPSAMPLE * fourier_order[i]*FARFIELD_UPSAMPLE;
      }
      if (Nelem) node_dsp[0] = 0;
      omp_par::scan(node_cnt.begin(), node_dsp.begin(), Nelem);
    }

    element_wise_node_cnt = node_cnt;
    const Long Nnodes = (Nelem ? node_dsp[Nelem-1]+node_cnt[Nelem-1] : 0);
    if (X       .Dim() != Nnodes*COORD_DIM) X       .ReInit(Nnodes*COORD_DIM);
    if (Xn      .Dim() != Nnodes*COORD_DIM) Xn      .ReInit(Nnodes*COORD_DIM);
    if (wts     .Dim() != Nnodes          ) wts     .ReInit(Nnodes          );
    if (dist_far.Dim() != Nnodes          ) dist_far.ReInit(Nnodes          );
    for (Long elem_idx = 0; elem_idx < Nelem; elem_idx++) {
      Vector<Real>        X_(node_cnt[elem_idx]*COORD_DIM,        X.begin()+node_dsp[elem_idx]*COORD_DIM, false);
      Vector<Real>       Xn_(node_cnt[elem_idx]*COORD_DIM,       Xn.begin()+node_dsp[elem_idx]*COORD_DIM, false);
      Vector<Real>      wts_(node_cnt[elem_idx]          ,      wts.begin()+node_dsp[elem_idx]          , false);
      Vector<Real> dist_far_(node_cnt[elem_idx]          , dist_far.begin()+node_dsp[elem_idx]          , false);

      Vector<Real> dX_ds, dX_dt; // TODO: pre-allocate
      const Long ChebOrder = cheb_order[elem_idx];
      const Long FourierOrder = fourier_order[elem_idx];
      const auto& leg_nds = LegendreQuadRule<Real>(ChebOrder*FARFIELD_UPSAMPLE).first;
      const auto& leg_wts = LegendreQuadRule<Real>(ChebOrder*FARFIELD_UPSAMPLE).second;
      GetGeom(&X_, &Xn_, &wts_, &dX_ds, &dX_dt, leg_nds, sin_theta<Real>(FourierOrder*FARFIELD_UPSAMPLE), cos_theta<Real>(FourierOrder*FARFIELD_UPSAMPLE), elem_idx);

      const Real theta_quad_wt = 2*const_pi<Real>()/(FourierOrder*FARFIELD_UPSAMPLE);
      for (Long i = 0; i < ChebOrder*FARFIELD_UPSAMPLE; i++) { // Set wts *= leg_wts * theta_quad_wt
        Real quad_wt = leg_wts[i] * theta_quad_wt;
        for (Long j = 0; j < FourierOrder*FARFIELD_UPSAMPLE; j++) {
          wts_[i*FourierOrder*FARFIELD_UPSAMPLE+j] *= quad_wt;
        }
      }
      for (Long i = 0; i < node_cnt[elem_idx]; i++) { // Set dist_far
        Real dxds = sqrt<Real>(dX_ds[i*COORD_DIM+0]*dX_ds[i*COORD_DIM+0] + dX_ds[i*COORD_DIM+1]*dX_ds[i*COORD_DIM+1] + dX_ds[i*COORD_DIM+2]*dX_ds[i*COORD_DIM+2])*const_pi<Real>()/2;
        Real dxdt = sqrt<Real>(dX_dt[i*COORD_DIM+0]*dX_dt[i*COORD_DIM+0] + dX_dt[i*COORD_DIM+1]*dX_dt[i*COORD_DIM+1] + dX_dt[i*COORD_DIM+2]*dX_dt[i*COORD_DIM+2])*const_pi<Real>()*2;
        Real h = std::max<Real>(dxds/(ChebOrder*FARFIELD_UPSAMPLE), dxdt/(FourierOrder*FARFIELD_UPSAMPLE));
        dist_far_[i] = -0.15 * log(tol)*h; // TODO: use better estimate
      }
    }
  }
  template <class Real> void SlenderElemList<Real>::GetFarFieldDensity(Vector<Real>& Fout, const Vector<Real>& Fin) {
    constexpr Integer MaxOrder = 50/FARFIELD_UPSAMPLE;
    auto compute_Mfourier_upsample_transpose = []() {
      Vector<Matrix<Real>> M_lst(MaxOrder);
      for (Long k = 1; k < MaxOrder; k++) {
        const Integer FourierOrder = k;
        const Integer FourierModes = FourierOrder/2+1;
        const Matrix<Real>& Mfourier_inv = fourier_matrix_inv<Real>(FourierOrder,FourierModes);
        const Matrix<Real>& Mfourier = fourier_matrix<Real>(FourierModes,FourierOrder*FARFIELD_UPSAMPLE);
        M_lst[k] = (Mfourier_inv * Mfourier).Transpose();
      }
      return M_lst;
    };
    auto compute_Mcheb_upsample_transpose = []() {
      Vector<Matrix<Real>> M_lst(MaxOrder);
      for (Long k = 0; k < MaxOrder; k++) {
        const Integer ChebOrder = k;
        Matrix<Real> Minterp(ChebOrder, ChebOrder*FARFIELD_UPSAMPLE);
        Vector<Real> Vinterp(ChebOrder*ChebOrder*FARFIELD_UPSAMPLE, Minterp.begin(), false);
        LagrangeInterp<Real>::Interpolate(Vinterp, CenterlineNodes(ChebOrder), LegendreQuadRule<Real>(ChebOrder*FARFIELD_UPSAMPLE).first);
        M_lst[k] = Minterp.Transpose();
      }
      return M_lst;
    };
    static const Vector<Matrix<Real>> Mfourier_transpose = compute_Mfourier_upsample_transpose();
    static const Vector<Matrix<Real>> Mcheb_transpose = compute_Mcheb_upsample_transpose();

    const Long Nelem = cheb_order.Dim();
    Vector<Long> node_cnt(Nelem), node_dsp(Nelem);
    { // Set node_cnt, node_dsp
      for (Long i = 0; i < Nelem; i++) {
        node_cnt[i] = cheb_order[i] * fourier_order[i];
      }
      if (Nelem) node_dsp[0] = 0;
      omp_par::scan(node_cnt.begin(), node_dsp.begin(), Nelem);
    }

    const Long Nnodes = (Nelem ? node_dsp[Nelem-1]+node_cnt[Nelem-1] : 0);
    const Long density_dof = Fin.Dim() / Nnodes;
    SCTL_ASSERT(Fin.Dim() == Nnodes * density_dof);

    if (Fout.Dim() != Nnodes*(FARFIELD_UPSAMPLE*FARFIELD_UPSAMPLE) * density_dof) {
      Fout.ReInit(Nnodes*(FARFIELD_UPSAMPLE*FARFIELD_UPSAMPLE) * density_dof);
    }
    for (Long i = 0; i < Nelem; i++) {
      const Integer ChebOrder = cheb_order[i];
      const Integer FourierOrder = fourier_order[i];

      const auto& Mfourier_ = Mfourier_transpose[FourierOrder];
      const Matrix<Real> Fin_(ChebOrder, FourierOrder*density_dof, (Iterator<Real>)Fin.begin()+node_dsp[i]*density_dof, false);
      Matrix<Real> F0_(ChebOrder, FourierOrder*FARFIELD_UPSAMPLE*density_dof);
      for (Long l = 0; l < ChebOrder; l++) { // Set F0
        for (Long j0 = 0; j0 < FourierOrder*FARFIELD_UPSAMPLE; j0++) {
          for (Long k = 0; k < density_dof; k++) {
            Real f = 0;
            for (Long j1 = 0; j1 < FourierOrder; j1++) {
              f += Fin_[l][j1*density_dof+k] * Mfourier_[j0][j1];
            }
            F0_[l][j0*density_dof+k] = f;
          }
        }
      }

      Matrix<Real> Fout_(ChebOrder*FARFIELD_UPSAMPLE, FourierOrder*FARFIELD_UPSAMPLE*density_dof, Fout.begin()+node_dsp[i]*FARFIELD_UPSAMPLE*FARFIELD_UPSAMPLE*density_dof, false);
      Matrix<Real>::GEMM(Fout_, Mcheb_transpose[ChebOrder], F0_);
    }
  }
  template <class Real> void SlenderElemList<Real>::FarFieldDensityOperatorTranspose(Matrix<Real>& Mout, const Matrix<Real>& Min, const Long elem_idx) {
    constexpr Integer MaxOrder = 50/FARFIELD_UPSAMPLE;
    auto compute_Mfourier_upsample = []() {
      Vector<Matrix<Real>> M_lst(MaxOrder);
      for (Long k = 1; k < MaxOrder; k++) {
        const Integer FourierOrder = k;
        const Integer FourierModes = FourierOrder/2+1;
        const Matrix<Real>& Mfourier_inv = fourier_matrix_inv<Real>(FourierOrder,FourierModes);
        const Matrix<Real>& Mfourier = fourier_matrix<Real>(FourierModes,FourierOrder*FARFIELD_UPSAMPLE);
        M_lst[k] = Mfourier_inv * Mfourier;
      }
      return M_lst;
    };
    auto compute_Mcheb_upsample = []() {
      Vector<Matrix<Real>> M_lst(MaxOrder);
      for (Long k = 0; k < MaxOrder; k++) {
        const Integer ChebOrder = k;
        Matrix<Real> Minterp(ChebOrder, ChebOrder*FARFIELD_UPSAMPLE);
        Vector<Real> Vinterp(ChebOrder*ChebOrder*FARFIELD_UPSAMPLE, Minterp.begin(), false);
        LagrangeInterp<Real>::Interpolate(Vinterp, CenterlineNodes(ChebOrder), LegendreQuadRule<Real>(ChebOrder*FARFIELD_UPSAMPLE).first);
        M_lst[k] = Minterp;
      }
      return M_lst;
    };
    static const Vector<Matrix<Real>> Mfourier = compute_Mfourier_upsample();
    static const Vector<Matrix<Real>> Mcheb = compute_Mcheb_upsample();

    const Integer ChebOrder = cheb_order[elem_idx];
    const Integer FourierOrder = fourier_order[elem_idx];

    const Long N = Min.Dim(1);
    const Long density_dof = Min.Dim(0) / (ChebOrder*FARFIELD_UPSAMPLE*FourierOrder*FARFIELD_UPSAMPLE);
    SCTL_ASSERT(Min.Dim(0) == ChebOrder*FARFIELD_UPSAMPLE*FourierOrder*FARFIELD_UPSAMPLE*density_dof);
    if (Mout.Dim(0) != ChebOrder*FourierOrder*density_dof || Mout.Dim(1) != N) {
      Mout.ReInit(ChebOrder*FourierOrder*density_dof,N);
      Mout.SetZero();
    }

    Matrix<Real> Mtmp(ChebOrder*FARFIELD_UPSAMPLE, FourierOrder*density_dof*N);
    const Matrix<Real> Min_(ChebOrder*FARFIELD_UPSAMPLE, FourierOrder*FARFIELD_UPSAMPLE*density_dof*N, (Iterator<Real>)Min.begin(), false);
    { // Appyl Mfourier
      const auto& Mfourier_ = Mfourier[FourierOrder];
      for (Long l = 0; l < ChebOrder*FARFIELD_UPSAMPLE; l++) {
        for (Long j0 = 0; j0 < FourierOrder; j0++) {
          for (Long k = 0; k < density_dof*N; k++) {
            Real f_tmp = 0;
            for (Long j1 = 0; j1 < FourierOrder*FARFIELD_UPSAMPLE; j1++) {
              f_tmp += Min_[l][j1*density_dof*N+k] * Mfourier_[j0][j1];
            }
            Mtmp[l][j0*density_dof*N+k] = f_tmp;
          }
        }
      }
    }

    Matrix<Real> Mout_(ChebOrder, FourierOrder*density_dof*N, Mout.begin(), false);
    Matrix<Real>::GEMM(Mout_, Mcheb[ChebOrder], Mtmp);
  }

  template <class Real> template <class Kernel> void SlenderElemList<Real>::SelfInterac(Vector<Matrix<Real>>& M_lst, const Kernel& ker, Real tol, const ElementListBase<Real>* self) {
    const auto& elem_lst = *dynamic_cast<const SlenderElemList*>(self);
    const Long Nelem = elem_lst.cheb_order.Dim();

    if (M_lst.Dim() != Nelem) M_lst.ReInit(Nelem);
    for (Long elem_idx = 0; elem_idx < Nelem; elem_idx++) {
      M_lst[elem_idx] = elem_lst.template SelfInteracHelper<Kernel>(ker, elem_idx, tol);
    }
  }
  template <class Real> template <class Kernel> void SlenderElemList<Real>::NearInterac(Matrix<Real>& M, const Vector<Real>& Xtrg, const Kernel& ker, Real tol, const Long elem_idx, const ElementListBase<Real>* self) {
    using Vec3 = Tensor<Real,true,COORD_DIM,1>;
    constexpr Integer KDIM0 = Kernel::SrcDim();
    constexpr Integer KDIM1 = Kernel::TrgDim();

    const auto& elem_lst = *dynamic_cast<const SlenderElemList*>(self);
    const Integer ChebOrder = elem_lst.cheb_order[elem_idx];
    const Integer FourierOrder = elem_lst.fourier_order[elem_idx];
    const Integer FourierModes = FourierOrder/2+1;
    const Integer digits = (Integer)(log(tol)/log(0.1)+0.5);
    const Matrix<Real> M_fourier_inv = fourier_matrix_inv<Real>(FourierOrder,FourierModes).Transpose(); // TODO: precompute

    const Vector<Real>  coord(COORD_DIM*ChebOrder,(Iterator<Real>)elem_lst. coord.begin()+COORD_DIM*elem_lst.elem_dsp[elem_idx],false);
    const Vector<Real>     dx(COORD_DIM*ChebOrder,(Iterator<Real>)elem_lst.    dx.begin()+COORD_DIM*elem_lst.elem_dsp[elem_idx],false);
    const Vector<Real>    d2x(COORD_DIM*ChebOrder,(Iterator<Real>)elem_lst.   d2x.begin()+COORD_DIM*elem_lst.elem_dsp[elem_idx],false);
    const Vector<Real> radius(        1*ChebOrder,(Iterator<Real>)elem_lst.radius.begin()+          elem_lst.elem_dsp[elem_idx],false);
    const Vector<Real>     dr(        1*ChebOrder,(Iterator<Real>)elem_lst.    dr.begin()+          elem_lst.elem_dsp[elem_idx],false);
    const Vector<Real>     e1(COORD_DIM*ChebOrder,(Iterator<Real>)elem_lst.    e1.begin()+COORD_DIM*elem_lst.elem_dsp[elem_idx],false);

    const Long Ntrg = Xtrg.Dim() / COORD_DIM;
    if (M.Dim(0) != ChebOrder*FourierOrder*KDIM0 || M.Dim(1) != Ntrg*KDIM1) {
      M.ReInit(ChebOrder*FourierOrder*KDIM0, Ntrg*KDIM1);
    }

    for (Long i = 0; i < Ntrg; i++) {
      const Vec3 Xt((Iterator<Real>)Xtrg.begin()+i*COORD_DIM);

      Matrix<Real> Mt(KDIM1, KDIM0*ChebOrder*FourierModes*2);
      { // Set Mt
        Vector<Real> quad_nds, quad_wts; // Quadrature rule in s
        auto adap_quad_rule = [&tol,&ChebOrder,&radius,&coord,&dx](Vector<Real>& quad_nds, Vector<Real>& quad_wts, const Vec3& x_trg) {
          const Long LegQuadOrder = 1*ChebOrder;
          const auto& leg_nds = LegendreQuadRule<Real>(LegQuadOrder).first;
          const auto& leg_wts = LegendreQuadRule<Real>(LegQuadOrder).second;
          auto adap_ref = [&LegQuadOrder,&leg_nds,&leg_wts](Vector<Real>& nds, Vector<Real>& wts, Real a, Real b, Integer levels) {
            if (nds.Dim() != levels * LegQuadOrder) nds.ReInit(levels*LegQuadOrder);
            if (wts.Dim() != levels * LegQuadOrder) wts.ReInit(levels*LegQuadOrder);
            Vector<Real> nds_(nds.Dim(), nds.begin(), false);
            Vector<Real> wts_(wts.Dim(), wts.begin(), false);

            while (levels) {
              Vector<Real> nds0(LegQuadOrder, nds_.begin(), false);
              Vector<Real> wts0(LegQuadOrder, wts_.begin(), false);
              Vector<Real> nds1((levels-1)*LegQuadOrder, nds_.begin()+LegQuadOrder, false);
              Vector<Real> wts1((levels-1)*LegQuadOrder, wts_.begin()+LegQuadOrder, false);

              Real end_point = (levels==1 ? b : (a+b)*0.5);
              nds0 = leg_nds * (end_point-a) + a;
              wts0 = leg_wts * fabs<Real>(end_point-a);

              nds_.Swap(nds1);
              wts_.Swap(wts1);
              a = end_point;
              levels--;
            }
          };

          // TODO: develop special quadrature rule instead of adaptive integration
          if (0) { // adaptive/dyadic refinement
            const Integer levels = 6;
            quad_nds.ReInit(2*levels*LegQuadOrder);
            quad_wts.ReInit(2*levels*LegQuadOrder);
            Vector<Real> nds0(levels*LegQuadOrder,quad_nds.begin(),false);
            Vector<Real> wts0(levels*LegQuadOrder,quad_wts.begin(),false);
            Vector<Real> nds1(levels*LegQuadOrder,quad_nds.begin()+levels*LegQuadOrder,false);
            Vector<Real> wts1(levels*LegQuadOrder,quad_wts.begin()+levels*LegQuadOrder,false);
            adap_ref(nds0, wts0, 0.5, 0.0, levels);
            adap_ref(nds1, wts1, 0.5, 1.0, levels);
          } else {
            Real dist_min, s_min, dxds;
            { // Set dist_min, s_min, dxds
              auto get_dist = [&ChebOrder,&radius,&coord,&dx] (const Vec3& x_trg, Real s) -> Real {
                Vector<Real> interp_wts(ChebOrder); // TODO: pre-allocate
                LagrangeInterp<Real>::Interpolate(interp_wts, CenterlineNodes(ChebOrder), Vector<Real>(1,Ptr2Itr<Real>(&s,1),false));

                Real r0 = 0;
                Vec3 x0, dx_ds0;
                for (Long i = 0; i < COORD_DIM; i++) {
                  x0(i,0) = 0;
                  dx_ds0(i,0) = 0;
                }
                for (Long i = 0; i < ChebOrder; i++) {
                  r0 += radius[i] * interp_wts[i];
                  x0(0,0) += coord[0*ChebOrder+i] * interp_wts[i];
                  x0(1,0) += coord[1*ChebOrder+i] * interp_wts[i];
                  x0(2,0) += coord[2*ChebOrder+i] * interp_wts[i];
                  dx_ds0(0,0) += dx[0*ChebOrder+i] * interp_wts[i];
                  dx_ds0(1,0) += dx[1*ChebOrder+i] * interp_wts[i];
                  dx_ds0(2,0) += dx[2*ChebOrder+i] * interp_wts[i];
                }
                Vec3 dx = x0 - x_trg;
                Vec3 n0 = dx_ds0 * sqrt<Real>(1/dot_prod(dx_ds0, dx_ds0));
                Real dz = dot_prod(dx, n0);
                Vec3 dr = dx - n0*dz;
                Real dR = sqrt<Real>(dot_prod(dr,dr)) - r0;
                return sqrt<Real>(dR*dR + dz*dz);
              };
              StaticArray<Real,2> dist;
              StaticArray<Real,2> s_val = {0,1};
              dist[0] = get_dist(x_trg, s_val[0]);
              dist[1] = get_dist(x_trg, s_val[1]);
              for (Long i = 0; i < 20; i++) { // Binary search: set dist, s_val
                Real ss = (s_val[0] + s_val[1]) * 0.5;
                Real dd = get_dist(x_trg, ss);
                if (dist[0] > dist[1]) {
                  dist[0] = dd;
                  s_val[0] = ss;
                } else {
                  dist[1] = dd;
                  s_val[1] = ss;
                }
              }
              if (dist[0] < dist[1]) { // Set dis_min, s_min
                dist_min = dist[0];
                s_min = s_val[0];
              } else {
                dist_min = dist[1];
                s_min = s_val[1];
              }
              { // Set dx_ds;
                Vector<Real> interp_wts(ChebOrder); // TODO: pre-allocate
                LagrangeInterp<Real>::Interpolate(interp_wts, CenterlineNodes(ChebOrder), Vector<Real>(1,Ptr2Itr<Real>(&s_min,1),false));

                Vec3 dxds_vec;
                for (Long i = 0; i < COORD_DIM; i++) {
                  dxds_vec(i,0) = 0;
                }
                for (Long i = 0; i < ChebOrder; i++) {
                  dxds_vec(0,0) += dx[0*ChebOrder+i] * interp_wts[i];
                  dxds_vec(1,0) += dx[1*ChebOrder+i] * interp_wts[i];
                  dxds_vec(2,0) += dx[2*ChebOrder+i] * interp_wts[i];
                }
                dxds = sqrt<Real>(dot_prod(dxds_vec,dxds_vec))*const_pi<Real>()/2;
              }
            }
            Real h0 =   (s_min)*dxds/LegQuadOrder;
            Real h1 = (1-s_min)*dxds/LegQuadOrder;
            Real dist_far0 = -0.15 * log(tol)*h0; // TODO: use better estimate
            Real dist_far1 = -0.15 * log(tol)*h1; // TODO: use better estimate
            Integer adap_levels0 = (s_min==0 ? 0 : std::max<Integer>(0,(Integer)(log(dist_far0/dist_min)/log(2.0)+0.5))+1);
            Integer adap_levels1 = (s_min==1 ? 0 : std::max<Integer>(0,(Integer)(log(dist_far1/dist_min)/log(2.0)+0.5))+1);

            Long N0 = adap_levels0 * LegQuadOrder;
            Long N1 = adap_levels1 * LegQuadOrder;
            quad_nds.ReInit(N0+N1);
            quad_wts.ReInit(N0+N1);
            Vector<Real> nds0(N0, quad_nds.begin(), false);
            Vector<Real> wts0(N0, quad_wts.begin(), false);
            Vector<Real> nds1(N1, quad_nds.begin()+N0, false);
            Vector<Real> wts1(N1, quad_wts.begin()+N0, false);
            adap_ref(nds0, wts0, 0, s_min, adap_levels0);
            adap_ref(nds1, wts1, 1, s_min, adap_levels1);
          }
        };
        adap_quad_rule(quad_nds, quad_wts, Xt);

        Matrix<Real> Minterp_quad_nds;
        { // Set Minterp_quad_nds
          Minterp_quad_nds.ReInit(ChebOrder, quad_nds.Dim());
          Vector<Real> Vinterp_quad_nds(ChebOrder*quad_nds.Dim(), Minterp_quad_nds.begin(), false);
          LagrangeInterp<Real>::Interpolate(Vinterp_quad_nds, CenterlineNodes(ChebOrder), quad_nds);
        }

        Vec3 x_trg = Xt;
        Matrix<Real> r_src, dr_src, x_src, dx_src, d2x_src, e1_src, e2_src, de1_src, de2_src;
        r_src  .ReInit(        1,quad_nds.Dim());
        dr_src .ReInit(        1,quad_nds.Dim());
        x_src  .ReInit(COORD_DIM,quad_nds.Dim());
        dx_src .ReInit(COORD_DIM,quad_nds.Dim());
        d2x_src.ReInit(COORD_DIM,quad_nds.Dim());
        e1_src .ReInit(COORD_DIM,quad_nds.Dim());
        e2_src .ReInit(COORD_DIM,quad_nds.Dim());
        de1_src.ReInit(COORD_DIM,quad_nds.Dim());
        de2_src.ReInit(COORD_DIM,quad_nds.Dim());
        { // Set x_src, x_trg (improve numerical stability)
          Matrix<Real> x_nodes(COORD_DIM,ChebOrder, (Iterator<Real>)coord.begin(), true);
          for (Long j = 0; j < ChebOrder; j++) {
            for (Integer k = 0; k < COORD_DIM; k++) {
              x_nodes[k][j] -= x_trg(k,0);
            }
          }
          Matrix<Real>::GEMM(  x_src, x_nodes, Minterp_quad_nds);
          for (Integer k = 0; k < COORD_DIM; k++) {
            x_trg(k,0) = 0;
          }
        }
        //Matrix<Real>::GEMM(  x_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>) coord.begin(),false), Minterp_quad_nds);
        Matrix<Real>::GEMM( dx_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)    dx.begin(),false), Minterp_quad_nds);
        Matrix<Real>::GEMM(d2x_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)   d2x.begin(),false), Minterp_quad_nds);
        Matrix<Real>::GEMM(  r_src, Matrix<Real>(        1,ChebOrder,(Iterator<Real>)radius.begin(),false), Minterp_quad_nds);
        Matrix<Real>::GEMM( dr_src, Matrix<Real>(        1,ChebOrder,(Iterator<Real>)    dr.begin(),false), Minterp_quad_nds);
        Matrix<Real>::GEMM( e1_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)    e1.begin(),false), Minterp_quad_nds);
        for (Long j = 0; j < quad_nds.Dim(); j++) { // Set e2_src
          Vec3 e1, dx, d2x;
          for (Integer k = 0; k < COORD_DIM; k++) {
            e1(k,0) = e1_src[k][j];
            dx(k,0) = dx_src[k][j];
            d2x(k,0) = d2x_src[k][j];
          }
          Real inv_dx2 = 1/dot_prod(dx,dx);
          e1 = e1 - dx * dot_prod(e1, dx) * inv_dx2;
          e1 = e1 * (1/sqrt<Real>(dot_prod(e1,e1)));

          Vec3 e2 = cross_prod(e1, dx);
          e2 = e2 * (1/sqrt<Real>(dot_prod(e2,e2)));
          Vec3 de1 = dx*(-dot_prod(e1,d2x) * inv_dx2);
          Vec3 de2 = dx*(-dot_prod(e2,d2x) * inv_dx2);
          for (Integer k = 0; k < COORD_DIM; k++) {
            e1_src[k][j] = e1(k,0);
            e2_src[k][j] = e2(k,0);
            de1_src[k][j] = de1(k,0);
            de2_src[k][j] = de2(k,0);
          }
        }

        const Vec3 y_trg = x_trg;
        Matrix<Real> M_tor(quad_nds.Dim(), FourierModes*2*KDIM0*KDIM1); // TODO: pre-allocate
        toroidal_greens_fn_batched(M_tor, y_trg, x_src, dx_src, d2x_src, r_src, dr_src, e1_src, e2_src, de1_src, de2_src, ker, FourierModes, digits);

        for (Long ii = 0; ii < M_tor.Dim(0); ii++) {
          for (Long jj = 0; jj < FourierModes*2*KDIM0*KDIM1; jj++) {
            M_tor[ii][jj] *= quad_wts[ii];
          }
        }
        Matrix<Real> M_(ChebOrder, FourierModes*2*KDIM0*KDIM1); // TODO: pre-allocate
        Matrix<Real>::GEMM(M_, Minterp_quad_nds, M_tor);

        for (Long ii = 0; ii < ChebOrder*FourierModes*2; ii++) { // Mt <-- M_
          for (Long k0 = 0; k0 < KDIM0; k0++) {
            for (Long k1 = 0; k1 < KDIM1; k1++) {
              Mt[k1][k0*ChebOrder*FourierModes*2+ii] = M_[0][(ii*KDIM0+k0)*KDIM1+k1];
            }
          }
        }
      }

      Matrix<Real> Mt_(KDIM1, KDIM0*ChebOrder*FourierOrder);
      { // Set Mt_
        Matrix<Real> M_nodal(KDIM1*KDIM0*ChebOrder, FourierOrder, Mt_.begin(), false);
        Matrix<Real> M_modal(KDIM1*KDIM0*ChebOrder, FourierModes*2, Mt.begin(), false);
        Matrix<Real>::GEMM(M_nodal, M_modal, M_fourier_inv);
      }

      { // Set M
        const Integer Nnds = ChebOrder*FourierOrder;
        for (Integer i0 = 0; i0 < Nnds; i0++) {
          for (Integer i1 = 0; i1 < KDIM0; i1++) {
            for (Integer j1 = 0; j1 < KDIM1; j1++) {
              M[i0*KDIM0+i1][i*KDIM1+j1] = Mt_[j1][i1*Nnds+i0];
            }
          }
        }
      }
    }
  }

  template <class Real> const Vector<Real>& SlenderElemList<Real>::CenterlineNodes(Integer Order) {
    return ChebQuadRule<Real>::nds(Order);
  }

  template <class Real> void SlenderElemList<Real>::GetVTUData(VTUData& vtu_data, const Vector<Real>& F, const Long elem_idx) const {
    if (elem_idx == -1) {
      const Long Nelem = cheb_order.Dim();
      Long dof = 0, offset = 0;
      if (F.Dim()) { // Set dof
        Long Nnodes = 0;
        for (Long i = 0; i < Nelem; i++) {
          Nnodes += cheb_order[i] * fourier_order[i];
        }
        dof = F.Dim() / Nnodes;
        SCTL_ASSERT(F.Dim() == Nnodes * dof);
      }
      for (Long i = 0; i < Nelem; i++) {
        const Vector<Real> F_(cheb_order[i]*fourier_order[i]*dof, (Iterator<Real>)F.begin()+offset, false);
        GetVTUData(vtu_data, F_, i);
        offset += F_.Dim();
      }
      return;
    }

    Vector<Real> X;
    const Integer ChebOrder = cheb_order[elem_idx];
    const Integer FourierOrder = fourier_order[elem_idx];
    GetGeom(&X,nullptr,nullptr,nullptr,nullptr, CenterlineNodes(ChebOrder), sin_theta<Real>(FourierOrder), cos_theta<Real>(FourierOrder), elem_idx);

    Long point_offset = vtu_data.coord.Dim() / COORD_DIM;
    for (const auto& x : X) vtu_data.coord.PushBack((VTUData::VTKReal)x);
    for (const auto& f : F) vtu_data.value.PushBack((VTUData::VTKReal)f);
    for (Long i = 0; i < ChebOrder-1; i++) {
      for (Long j = 0; j <= FourierOrder; j++) {
        vtu_data.connect.PushBack(point_offset + (i+0)*FourierOrder+(j%FourierOrder));
        vtu_data.connect.PushBack(point_offset + (i+1)*FourierOrder+(j%FourierOrder));
      }
      vtu_data.offset.PushBack(vtu_data.connect.Dim());
      vtu_data.types.PushBack(6);
    }
  }
  template <class Real> void SlenderElemList<Real>::WriteVTK(std::string fname, const Vector<Real>& F, const Comm& comm) const {
    VTUData vtu_data;
    GetVTUData(vtu_data, F);
    vtu_data.WriteVTK(fname, comm);
  }

  template <class Real> void SlenderElemList<Real>::test() {
    if (Comm::World().Rank()) return; // execute on one MPI process
    sctl::Profile::Enable(false);

    SlenderElemList elem_lst0;
    { // Set elem_lst0
      Vector<Real> coord, radius;
      Vector<Long> cheb_order, fourier_order;
      const Long Nelem = 16, ChebOrder = 10, FourierOrder = 8;
      for (Long k = 0; k < Nelem; k++) { // Set cheb_order, fourier_order, coord, radius
        const auto& nds = CenterlineNodes(ChebOrder);
        for (Long i = 0; i < nds.Dim(); i++) {
          Real theta = 2*const_pi<Real>()*(k+nds[i])/Nelem;
          coord.PushBack(cos<Real>(theta));
          coord.PushBack(sin<Real>(theta));
          coord.PushBack(0.1*sin<Real>(2*theta));
          radius.PushBack(0.01*(2+sin<Real>(theta+sqrt<Real>(2))));
        }
        cheb_order.PushBack(ChebOrder);
        fourier_order.PushBack(FourierOrder);
      }
      elem_lst0.Init(cheb_order, fourier_order, coord, radius);
    }

    GenericKernel<sctl::Laplace3D_DxU> laplace_dl;
    BoundaryIntegralOp<Real,GenericKernel<sctl::Laplace3D_DxU>> LapDL(laplace_dl);
    LapDL.AddElemList(elem_lst0);

    // Warm-up run
    Vector<Real> F(LapDL.Dim(0)), U; F = 1;
    LapDL.ComputePotential(U,F);
    LapDL.ClearSetup();

    sctl::Profile::Enable(true);
    Profile::Tic("Setup+Eval");
    LapDL.ComputePotential(U,F);
    Profile::Toc();

    U = 0;
    Profile::Tic("Eval");
    LapDL.ComputePotential(U,F);
    Profile::Toc();

    Vector<Real> Uerr = U*(1/(4*const_pi<Real>())) + 0.5;
    elem_lst0.WriteVTK("Uerr", Uerr); // Write VTK
    { // Print error
      Real max_err = 0;
      for (auto x : Uerr) max_err = std::max<Real>(max_err, fabs(x));
      std::cout<<"Error = "<<max_err<<'\n';
    }
    sctl::Profile::Enable(false);
    sctl::Profile::print();
  }
  template <class Real> void SlenderElemList<Real>::test_greens_identity() {
    const auto concat_vecs = [](Vector<Real>& v, const Vector<Vector<Real>>& vec_lst) {
      const Long N = vec_lst.Dim();
      Vector<Long> dsp(N+1); dsp[0] = 0;
      for (Long i = 0; i < N; i++) {
        dsp[i+1] = dsp[i] + vec_lst[i].Dim();
      }
      if (v.Dim() != dsp[N]) v.ReInit(dsp[N]);
      for (Long i = 0; i < N; i++) {
        Vector<Real> v_(vec_lst[i].Dim(), v.begin()+dsp[i], false);
        v_ = vec_lst[i];
      }
    };
    auto loop_geom = [](Real& x, Real& y, Real& z, Real& r, const Real theta){
      x = cos<Real>(theta);
      y = sin<Real>(theta);
      z = 0.1*sin<Real>(theta-sqrt<Real>(2));
      r = 0.01*(2+sin<Real>(theta+sqrt<Real>(2)));
    };
    const Comm comm = Comm::World();
    sctl::Profile::Enable(false);

    SlenderElemList elem_lst0;
    SlenderElemList elem_lst1;
    { // Set elem_lst0, elem_lst1
      const Long Nelem = 16;
      const Long Np = comm.Size();
      const Long rank = comm.Rank();
      const Long idx0 = Nelem*(rank+0)/Np;
      const Long idx1 = Nelem*(rank+1)/Np;

      Vector<Real> coord0, radius0;
      Vector<Long> cheb_order0, fourier_order0;
      for (Long k = idx0; k < idx1; k++) { // Init elem_lst0
      const Integer ChebOrder = 8, FourierOrder = 14;
        const auto& nds = CenterlineNodes(ChebOrder);
        for (Long i = 0; i < nds.Dim(); i++) {
          Real x, y, z, r;
          loop_geom(x, y, z, r, const_pi<Real>()*(k+nds[i])/Nelem);
          coord0.PushBack(x);
          coord0.PushBack(y);
          coord0.PushBack(z);
          radius0.PushBack(r);
        }
        cheb_order0.PushBack(ChebOrder);
        fourier_order0.PushBack(FourierOrder);
      }
      elem_lst0.Init(cheb_order0, fourier_order0, coord0, radius0);

      Vector<Real> coord1, radius1;
      Vector<Long> cheb_order1, fourier_order1;
      for (Long k = idx0; k < idx1; k++) { // Init elem_lst1
        const Integer ChebOrder = 10, FourierOrder = 14;
        const auto& nds = CenterlineNodes(ChebOrder);
        for (Long i = 0; i < nds.Dim(); i++) {
          Real x, y, z, r;
          loop_geom(x, y, z, r, const_pi<Real>()*(1+(k+nds[i])/Nelem));
          coord1.PushBack(x);
          coord1.PushBack(y);
          coord1.PushBack(z);
          radius1.PushBack(r);
        }
        cheb_order1.PushBack(ChebOrder);
        fourier_order1.PushBack(FourierOrder);
      }
      elem_lst1.Init(cheb_order1, fourier_order1, coord1, radius1);
    }

    GenericKernel<sctl::Laplace3D_FxU> laplace_sl;
    GenericKernel<sctl::Laplace3D_DxU> laplace_dl;
    GenericKernel<sctl::Laplace3D_FxdU> laplace_grad;
    BoundaryIntegralOp<Real,GenericKernel<sctl::Laplace3D_FxU>> LapSL(laplace_sl, comm);
    BoundaryIntegralOp<Real,GenericKernel<sctl::Laplace3D_DxU>> LapDL(laplace_dl, comm);
    LapSL.AddElemList(elem_lst0, "elem_lst0");
    LapSL.AddElemList(elem_lst1, "elem_lst1");
    LapDL.AddElemList(elem_lst0, "elem_lst0");
    LapDL.AddElemList(elem_lst1, "elem_lst1");

    Vector<Real> X, Xn, Fs, Fd, Uref, Us, Ud;
    { // Get X, Xn
      Vector<Vector<Real>> X_(2), Xn_(2);
      elem_lst0.GetNodeCoord(&X_[0], &Xn_[0], nullptr);
      elem_lst1.GetNodeCoord(&X_[1], &Xn_[1], nullptr);
      concat_vecs(X, X_);
      concat_vecs(Xn, Xn_);
    }
    { // Set Fs, Fd, Uref
      Vector<Real> X0{0.3,0.6,0.2}, Xn0{0,0,0}, F0{1}, dU;
      laplace_sl.Eval(Uref, X, X0, Xn0, F0);
      laplace_grad.Eval(dU, X, X0, Xn0, F0);

      Fd = Uref;
      { // Set Fs <-- -dot_prod(dU, Xn)
        Fs.ReInit(X.Dim()/COORD_DIM);
        for (Long i = 0; i < Fs.Dim(); i++) {
          Real dU_dot_Xn = 0;
          for (Long k = 0; k < COORD_DIM; k++) {
            dU_dot_Xn += dU[i*COORD_DIM+k] * Xn[i*COORD_DIM+k];
          }
          Fs[i] = -dU_dot_Xn;
        }
      }
    }

    // Warm-up run
    LapSL.ComputePotential(Us,Fs);
    LapDL.ComputePotential(Ud,Fd);
    LapSL.ClearSetup();
    LapDL.ClearSetup();

    sctl::Profile::Enable(true);
    Profile::Tic("Setup+Eval", &comm);
    LapSL.ComputePotential(Us,Fs);
    LapDL.ComputePotential(Ud,Fd);
    Profile::Toc();

    Us = 0; Ud = 0;
    Profile::Tic("Eval", &comm);
    LapSL.ComputePotential(Us,Fs);
    LapDL.ComputePotential(Ud,Fd);
    Profile::Toc();

    { // Write VTK
      Vector<Real> Uerr = Fd*0.5 + (Us - Ud)*(1/(4*const_pi<Real>())) - Uref;

      Vector<Vector<Real>> X_(2);
      elem_lst0.GetNodeCoord(&X_[0], nullptr, nullptr);
      elem_lst1.GetNodeCoord(&X_[1], nullptr, nullptr);
      const Long N0 = X_[0].Dim()/COORD_DIM;
      const Long N1 = X_[1].Dim()/COORD_DIM;
      elem_lst0.WriteVTK("Uerr0", Vector<Real>(N0,Uerr.begin()+ 0,false), comm);
      elem_lst1.WriteVTK("Uerr1", Vector<Real>(N1,Uerr.begin()+N0,false), comm);
      { // Print error
        Real max_err = 0, max_val = 0;
        for (auto x : Uerr) max_err = std::max<Real>(max_err, fabs(x));
        for (auto x : Uref) max_val = std::max<Real>(max_val, fabs(x));
        std::cout<<"Error = "<<max_err/max_val<<'\n';
      }
    }

    sctl::Profile::print(&comm);
    sctl::Profile::Enable(false);
  }

  template <class Real> void SlenderElemList<Real>::GetGeom(Vector<Real>* X, Vector<Real>* Xn, Vector<Real>* Xa, Vector<Real>* dX_ds, Vector<Real>* dX_dt, const Vector<Real>& s_param, const Vector<Real>& sin_theta_, const Vector<Real>& cos_theta_, const Long elem_idx) const {
    using Vec3 = Tensor<Real,true,COORD_DIM,1>;
    const Integer ChebOrder = cheb_order[elem_idx];
    const Long Nt = sin_theta_.Dim();
    const Long Ns = s_param.Dim();
    const Long N = Ns * Nt;

    if (X     && X    ->Dim() != N*COORD_DIM) X    ->ReInit(N*COORD_DIM);
    if (Xn    && Xn   ->Dim() != N*COORD_DIM) Xn   ->ReInit(N*COORD_DIM);
    if (Xa    && Xa   ->Dim() != N          ) Xa   ->ReInit(N);
    if (dX_ds && dX_ds->Dim() != N*COORD_DIM) dX_ds->ReInit(N*COORD_DIM);
    if (dX_dt && dX_dt->Dim() != N*COORD_DIM) dX_dt->ReInit(N*COORD_DIM);

    Matrix<Real> M_lagrange_interp;
    { // Set M_lagrange_interp
      M_lagrange_interp.ReInit(ChebOrder, Ns);
      Vector<Real> V_lagrange_interp(ChebOrder*Ns, M_lagrange_interp.begin(), false);
      LagrangeInterp<Real>::Interpolate(V_lagrange_interp, CenterlineNodes(ChebOrder), s_param);
    }

    Matrix<Real> r_, dr_, x_, dx_, d2x_, e1_;
    r_  .ReInit(        1,Ns);
    x_  .ReInit(COORD_DIM,Ns);
    dx_ .ReInit(COORD_DIM,Ns);
    e1_ .ReInit(COORD_DIM,Ns);
    Matrix<Real>::GEMM(  x_, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>) coord.begin()+COORD_DIM*elem_dsp[elem_idx],false), M_lagrange_interp);
    Matrix<Real>::GEMM( dx_, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)    dx.begin()+COORD_DIM*elem_dsp[elem_idx],false), M_lagrange_interp);
    Matrix<Real>::GEMM(  r_, Matrix<Real>(        1,ChebOrder,(Iterator<Real>)radius.begin()+          elem_dsp[elem_idx],false), M_lagrange_interp);
    Matrix<Real>::GEMM( e1_, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)    e1.begin()+COORD_DIM*elem_dsp[elem_idx],false), M_lagrange_interp);
    if (Xn || Xa) { // Set dr_, d2x_
      dr_ .ReInit(        1,Ns);
      d2x_.ReInit(COORD_DIM,Ns);
      Matrix<Real>::GEMM(d2x_, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)   d2x.begin()+COORD_DIM*elem_dsp[elem_idx],false), M_lagrange_interp);
      Matrix<Real>::GEMM( dr_, Matrix<Real>(        1,ChebOrder,(Iterator<Real>)    dr.begin()+          elem_dsp[elem_idx],false), M_lagrange_interp);
    }
    auto compute_coord = [](Vec3& y, const Vec3& x, const Vec3& e1, const Vec3& e2, const Real r, const Real sint, const Real cost) {
      y = x + e1*(r*cost) + e2*(r*sint);
    };
    auto compute_normal_area_elem_tangents = [](Vec3& n, Real& da, Vec3& dy_ds, Vec3& dy_dt, const Vec3& dx, const Vec3& e1, const Vec3& e2, const Vec3& de1, const Vec3& de2, const Real r, const Real dr, const Real sint, const Real cost) {
      dy_ds = dx + e1*(dr*cost) + e2*(dr*sint) + de1*(r*cost) + de2*(r*sint);
      dy_dt = e1*(-r*sint) + e2*(r*cost);

      n = cross_prod(dy_ds, dy_dt);
      da = sqrt<Real>(dot_prod(n,n));
      n = n * (1/da);
    };

    for (Long j = 0; j < Ns; j++) {
      Real r, inv_dx2;
      Vec3 x, dx, e1, e2;
      { // Set x, dx, e1, r, inv_dx2
        for (Integer k = 0; k < COORD_DIM; k++) {
          x(k,0)  = x_[k][j];
          dx(k,0) = dx_[k][j];
          e1(k,0) = e1_[k][j];
        }
        inv_dx2 = 1/dot_prod(dx,dx);
        r = r_[0][j];

        e1 = e1 - dx * dot_prod(e1, dx) * inv_dx2;
        e1 = e1 * (1/sqrt<Real>(dot_prod(e1,e1)));

        e2 = cross_prod(e1, dx);
        e2 = e2 * (1/sqrt<Real>(dot_prod(e2,e2)));
      }

      if (X) {
        for (Integer i = 0; i < Nt; i++) { // Set X
          Vec3 y;
          compute_coord(y, x, e1, e2, r, sin_theta_[i], cos_theta_[i]);
          for (Integer k = 0; k < COORD_DIM; k++) {
            (*X)[(j*Nt+i)*COORD_DIM+k] = y(k,0);
          }
        }
      }
      if (Xn || Xa || dX_ds || dX_dt) {
        Vec3 d2x, de1, de2;
        for (Integer k = 0; k < COORD_DIM; k++) {
          d2x(k,0) = d2x_[k][j];
        }
        de1 = dx*(-dot_prod(e1,d2x) * inv_dx2);
        de2 = dx*(-dot_prod(e2,d2x) * inv_dx2);
        Real dr = dr_[0][j];

        for (Integer i = 0; i < Nt; i++) { // Set X, Xn, Xa, dX_ds, dX_dt
          Real da;
          Vec3 n, dx_ds, dx_dt;
          compute_normal_area_elem_tangents(n, da, dx_ds, dx_dt, dx, e1, e2, de1, de2, r, dr, sin_theta_[i], cos_theta_[i]);
          if (Xn) {
            for (Integer k = 0; k < COORD_DIM; k++) {
              (*Xn)[(j*Nt+i)*COORD_DIM+k] = n(k,0);
            }
          }
          if (Xa) {
            (*Xa)[j*Nt+i] = da;
          }
          if (dX_ds) {
            for (Integer k = 0; k < COORD_DIM; k++) {
              (*dX_ds)[(j*Nt+i)*COORD_DIM+k] = dx_ds(k,0);
            }
          }
          if (dX_dt) {
            for (Integer k = 0; k < COORD_DIM; k++) {
              (*dX_dt)[(j*Nt+i)*COORD_DIM+k] = dx_dt(k,0);
            }
          }
        }
      }
    }
  }

  template <class Real> template <class Kernel> Matrix<Real> SlenderElemList<Real>::SelfInteracHelper_(const Kernel& ker, const Long elem_idx, const Real tol) const { // constant radius
    using Vec3 = Tensor<Real,true,COORD_DIM,1>;
    constexpr Integer KDIM0 = Kernel::SrcDim();
    constexpr Integer KDIM1 = Kernel::TrgDim();
    constexpr Integer KerScaleExp=-2; // for laplace double-layer // TODO: determine this automatically
    constexpr Integer FOURIER_ORDER = 8;

    static ToroidalGreensFn<Real,FOURIER_ORDER> tor_greens_fn;
    { // Setup tor_greens_fn
      static bool first_time = true;
      #pragma omp critical
      if (first_time) {
        tor_greens_fn.Setup(ker,1.0);
        first_time = false;
      }
    }

    const Integer ChebOrder = cheb_order[elem_idx];
    const Integer FourierOrder = fourier_order[elem_idx];
    SCTL_ASSERT(FourierOrder == FOURIER_ORDER);
    const Integer FourierModes = FourierOrder/2+1;
    const Integer digits = (Integer)(log(tol)/log(0.1)+0.5);
    const Matrix<Real> M_fourier_inv = fourier_matrix_inv<Real>(FourierOrder,FourierModes).Transpose(); // TODO: precompute

    const Vector<Real>  coord(COORD_DIM*ChebOrder,(Iterator<Real>)this-> coord.begin()+COORD_DIM*elem_dsp[elem_idx],false);
    const Vector<Real>     dx(COORD_DIM*ChebOrder,(Iterator<Real>)this->    dx.begin()+COORD_DIM*elem_dsp[elem_idx],false);
    const Vector<Real>    d2x(COORD_DIM*ChebOrder,(Iterator<Real>)this->   d2x.begin()+COORD_DIM*elem_dsp[elem_idx],false);
    const Vector<Real> radius(        1*ChebOrder,(Iterator<Real>)this->radius.begin()+          elem_dsp[elem_idx],false);
    const Vector<Real>     dr(        1*ChebOrder,(Iterator<Real>)this->    dr.begin()+          elem_dsp[elem_idx],false);
    const Vector<Real>     e1(COORD_DIM*ChebOrder,(Iterator<Real>)this->    e1.begin()+COORD_DIM*elem_dsp[elem_idx],false);

    const Real dtheta = 2*const_pi<Real>()/FourierOrder;
    const Complex<Real> exp_dtheta(cos<Real>(dtheta), sin<Real>(dtheta));

    Matrix<Real> Mt(KDIM1*ChebOrder*FourierOrder, KDIM0*ChebOrder*FourierModes*2);
    for (Long i = 0; i < ChebOrder; i++) {
      Real r_trg = radius[i];
      Real s_trg = CenterlineNodes(ChebOrder)[i];
      Vec3 x_trg, dx_trg, e1_trg, e2_trg;
      { // Set x_trg, e1_trg, e2_trg
        for (Integer k = 0; k < COORD_DIM; k++) {
          x_trg (k,0) = coord[k*ChebOrder+i];
          e1_trg(k,0) = e1[k*ChebOrder+i];
          dx_trg(k,0) = dx[k*ChebOrder+i];
        }
        e2_trg = cross_prod(e1_trg, dx_trg);
        e2_trg = e2_trg * (1/sqrt<Real>(dot_prod(e2_trg,e2_trg)));
      }

      Vector<Real> quad_nds, quad_wts; // Quadrature rule in s
      SpecialQuadRule(quad_nds, quad_wts, ChebOrder, s_trg, r_trg, sqrt<Real>(dot_prod(dx_trg, dx_trg)), digits);

      Matrix<Real> Minterp_quad_nds;
      { // Set Minterp_quad_nds
        Minterp_quad_nds.ReInit(ChebOrder, quad_nds.Dim());
        Vector<Real> Vinterp_quad_nds(ChebOrder*quad_nds.Dim(), Minterp_quad_nds.begin(), false);
        LagrangeInterp<Real>::Interpolate(Vinterp_quad_nds, CenterlineNodes(ChebOrder), quad_nds);
      }

      Matrix<Real> r_src, dr_src, x_src, dx_src, d2x_src, e1_src, e2_src, de1_src, de2_src;
      r_src  .ReInit(        1,quad_nds.Dim());
      dr_src .ReInit(        1,quad_nds.Dim());
      x_src  .ReInit(COORD_DIM,quad_nds.Dim());
      dx_src .ReInit(COORD_DIM,quad_nds.Dim());
      d2x_src.ReInit(COORD_DIM,quad_nds.Dim());
      e1_src .ReInit(COORD_DIM,quad_nds.Dim());
      e2_src .ReInit(COORD_DIM,quad_nds.Dim());
      de1_src.ReInit(COORD_DIM,quad_nds.Dim());
      de2_src.ReInit(COORD_DIM,quad_nds.Dim());
      { // Set x_src, x_trg (improve numerical stability)
        Matrix<Real> x_nodes(COORD_DIM,ChebOrder, (Iterator<Real>)coord.begin(), true);
        for (Long j = 0; j < ChebOrder; j++) {
          for (Integer k = 0; k < COORD_DIM; k++) {
            x_nodes[k][j] -= x_trg(k,0);
          }
        }
        Matrix<Real>::GEMM(  x_src, x_nodes, Minterp_quad_nds);
        for (Integer k = 0; k < COORD_DIM; k++) {
          x_trg(k,0) = 0;
        }
      }
      //Matrix<Real>::GEMM(  x_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>) coord.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM( dx_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)    dx.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM(d2x_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)   d2x.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM(  r_src, Matrix<Real>(        1,ChebOrder,(Iterator<Real>)radius.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM( dr_src, Matrix<Real>(        1,ChebOrder,(Iterator<Real>)    dr.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM( e1_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)    e1.begin(),false), Minterp_quad_nds);
      for (Long j = 0; j < quad_nds.Dim(); j++) { // Set e2_src
        Vec3 e1, dx, d2x;
        for (Integer k = 0; k < COORD_DIM; k++) {
          e1(k,0) = e1_src[k][j];
          dx(k,0) = dx_src[k][j];
          d2x(k,0) = d2x_src[k][j];
        }
        Real inv_dx2 = 1/dot_prod(dx,dx);
        e1 = e1 - dx * dot_prod(e1, dx) * inv_dx2;
        e1 = e1 * (1/sqrt<Real>(dot_prod(e1,e1)));

        Vec3 e2 = cross_prod(e1, dx);
        e2 = e2 * (1/sqrt<Real>(dot_prod(e2,e2)));
        Vec3 de1 = dx*(-dot_prod(e1,d2x) * inv_dx2);
        Vec3 de2 = dx*(-dot_prod(e2,d2x) * inv_dx2);
        for (Integer k = 0; k < COORD_DIM; k++) {
          e1_src[k][j] = e1(k,0);
          e2_src[k][j] = e2(k,0);
          de1_src[k][j] = de1(k,0);
          de2_src[k][j] = de2(k,0);
        }
      }

      Complex<Real> exp_theta_trg(1,0);
      for (Long j = 0; j < FourierOrder; j++) {
        const Vec3 y_trg = x_trg + e1_trg*r_trg*exp_theta_trg.real + e2_trg*r_trg*exp_theta_trg.imag;

        Matrix<Real> M_tor(quad_nds.Dim(), FourierModes*2*KDIM0 * KDIM1); // TODO: pre-allocate
        auto toroidal_greens_fn_batched = [this,&ker,&FourierOrder,&FourierModes](Matrix<Real>& M, const Vec3& y_trg, const Matrix<Real>& x_src, const Matrix<Real>& dx_src, const Matrix<Real>& d2x_src, const Matrix<Real>& r_src, const Matrix<Real>& dr_src, const Matrix<Real>& e1_src, const Matrix<Real>& e2_src, const Matrix<Real>& de1_src, const Matrix<Real>& de2_src){
          const Long BatchSize = M.Dim(0);
          SCTL_ASSERT(  x_src.Dim(1) == BatchSize);
          SCTL_ASSERT( dx_src.Dim(1) == BatchSize);
          SCTL_ASSERT(d2x_src.Dim(1) == BatchSize);
          SCTL_ASSERT(  r_src.Dim(1) == BatchSize);
          SCTL_ASSERT( dr_src.Dim(1) == BatchSize);
          SCTL_ASSERT( e1_src.Dim(1) == BatchSize);
          SCTL_ASSERT( e2_src.Dim(1) == BatchSize);
          SCTL_ASSERT(de1_src.Dim(1) == BatchSize);
          SCTL_ASSERT(de2_src.Dim(1) == BatchSize);
          SCTL_ASSERT(M.Dim(1) == FourierModes*2*KDIM0 * KDIM1);
          for (Long ii = 0; ii < BatchSize; ii++) {
            Real r = r_src[0][ii]; //, dr = dr_src[0][ii];
            Vec3 x, dx, d2x, e1, e2, de1, de2;
            { // Set x, dx, d2x, e1, e2, de1, de2
              for (Integer k = 0; k < COORD_DIM; k++) {
                x  (k,0) =   x_src[k][ii];
                dx (k,0) =  dx_src[k][ii];
                d2x(k,0) = d2x_src[k][ii];
                e1 (k,0) =  e1_src[k][ii];
                e2 (k,0) =  e2_src[k][ii];
                de1(k,0) = de1_src[k][ii];
                de2(k,0) = de2_src[k][ii];
              }
            }

            Matrix<Real> M_toroidal_greens_fn(KDIM0*FourierModes*2, KDIM1, M[ii], false);
            //toroidal_greens_fn(M_toroidal_greens_fn, y_trg, x, dx, d2x, e1, e2, de1, de2, r, dr);

            tor_greens_fn.BuildOperatorModal(M_toroidal_greens_fn, dot_prod(y_trg-x,e1)/r, dot_prod(y_trg-x,e2)/r, dot_prod(y_trg-x,cross_prod(e1,e2))/r, ker);
            { // Scale M_toroidal_greens_fn
              Real scale = sqrt(2.0) * sctl::pow<KerScaleExp>(r);
              for (Long i = 0; i < KDIM0; i++) {
                for (Long k = 0; k < FourierModes; k++) {
                  for (Long j = 0; j < KDIM1; j++) {
                    M_toroidal_greens_fn[i*FourierModes*2+k*2+0][j] *= scale;
                    M_toroidal_greens_fn[i*FourierModes*2+k*2+1][j] *=-scale;
                  }
                }
              }
              for (Long i = 0; i < KDIM0; i++) {
                for (Long j = 0; j < KDIM1; j++) {
                  M_toroidal_greens_fn[i*FourierModes*2+0][j] *= 2;
                  M_toroidal_greens_fn[i*FourierModes*2+1][j] *= 2;
                  if (FourierOrder%2 == 0) {
                    M_toroidal_greens_fn[(i+1)*FourierModes*2-2][j] *= 2;
                    M_toroidal_greens_fn[(i+1)*FourierModes*2-1][j] *= 2;
                  }
                }
              }
            }
          }
        };
        toroidal_greens_fn_batched(M_tor, y_trg, x_src, dx_src, d2x_src, r_src, dr_src, e1_src, e2_src, de1_src, de2_src);

        Matrix<Real> M_(ChebOrder, FourierModes*2*KDIM0 * KDIM1); // TODO: pre-allocate
        for (Long ii = 0; ii < M_tor.Dim(0); ii++) {
          Matrix<Real> M_tor_(M_tor.Dim(1), KDIM1, M_tor[ii], false);
          M_tor_ *= quad_wts[ii];
        }
        Matrix<Real>::GEMM(M_, Minterp_quad_nds, M_tor);

        for (Long ii = 0; ii < ChebOrder*FourierModes*2; ii++) { // Mt <-- M_
          for (Long k0 = 0; k0 < KDIM0; k0++) {
            for (Long k1 = 0; k1 < KDIM1; k1++) {
              Mt[(k1*ChebOrder+i)*FourierOrder+j][k0*ChebOrder*FourierModes*2+ii] = M_[0][(ii*KDIM0+k0)*KDIM1+k1];
            }
          }
        }
        exp_theta_trg *= exp_dtheta;
      }
    }

    Matrix<Real> Mt_(KDIM1*ChebOrder*FourierOrder, KDIM0*ChebOrder*FourierOrder);
    { // Set Mt_
      const Matrix<Real> M_modal(KDIM1*ChebOrder*FourierOrder*KDIM0*ChebOrder, FourierModes*2, Mt.begin(), false);
      Matrix<Real> M_nodal(KDIM1*ChebOrder*FourierOrder*KDIM0*ChebOrder, FourierOrder, Mt_.begin(), false);
      Matrix<Real>::GEMM(M_nodal, M_modal, M_fourier_inv);
    }
    { // Mt_ <-- Mt_ * Xa
      Vector<Real> Xa;
      GetGeom(nullptr, nullptr, &Xa, nullptr, nullptr, CenterlineNodes(ChebOrder), sin_theta<Real>(FourierOrder), cos_theta<Real>(FourierOrder), elem_idx);
      SCTL_ASSERT(Xa.Dim() == ChebOrder*FourierOrder);
      for (Long k = 0; k < KDIM1*ChebOrder*FourierOrder; k++) {
        for (Long i = 0; i < KDIM0; i++) {
          for (Long j = 0; j < ChebOrder*FourierOrder; j++) {
            Mt_[k][i*ChebOrder*FourierOrder+j] *= Xa[j];
          }
        }
      }
    }

    Matrix<Real> M(ChebOrder*FourierOrder*KDIM0, ChebOrder*FourierOrder*KDIM1);
    { // Set M
      const Integer Nnds = ChebOrder*FourierOrder;
      for (Integer i0 = 0; i0 < Nnds; i0++) {
        for (Integer i1 = 0; i1 < KDIM0; i1++) {
          for (Integer j0 = 0; j0 < Nnds; j0++) {
            for (Integer j1 = 0; j1 < KDIM1; j1++) {
              M[i0*KDIM0+i1][j0*KDIM1+j1] = Mt_[j1*Nnds+j0][i1*Nnds+i0];
            }
          }
        }
      }
    }
    return M;
  }
  template <class Real> template <class Kernel> Matrix<Real> SlenderElemList<Real>::SelfInteracHelper(const Kernel& ker, const Long elem_idx, const Real tol) const {
    using Vec3 = Tensor<Real,true,COORD_DIM,1>;
    constexpr Integer KDIM0 = Kernel::SrcDim();
    constexpr Integer KDIM1 = Kernel::TrgDim();

    const Integer ChebOrder = cheb_order[elem_idx];
    const Integer FourierOrder = fourier_order[elem_idx];
    const Integer FourierModes = FourierOrder/2+1;
    const Integer digits = (Integer)(log(tol)/log(0.1)+0.5);
    const Matrix<Real> M_fourier_inv = fourier_matrix_inv<Real>(FourierOrder,FourierModes).Transpose(); // TODO: precompute

    const Vector<Real>  coord(COORD_DIM*ChebOrder,(Iterator<Real>)this-> coord.begin()+COORD_DIM*elem_dsp[elem_idx],false);
    const Vector<Real>     dx(COORD_DIM*ChebOrder,(Iterator<Real>)this->    dx.begin()+COORD_DIM*elem_dsp[elem_idx],false);
    const Vector<Real>    d2x(COORD_DIM*ChebOrder,(Iterator<Real>)this->   d2x.begin()+COORD_DIM*elem_dsp[elem_idx],false);
    const Vector<Real> radius(        1*ChebOrder,(Iterator<Real>)this->radius.begin()+          elem_dsp[elem_idx],false);
    const Vector<Real>     dr(        1*ChebOrder,(Iterator<Real>)this->    dr.begin()+          elem_dsp[elem_idx],false);
    const Vector<Real>     e1(COORD_DIM*ChebOrder,(Iterator<Real>)this->    e1.begin()+COORD_DIM*elem_dsp[elem_idx],false);

    const Real dtheta = 2*const_pi<Real>()/FourierOrder;
    const Complex<Real> exp_dtheta(cos<Real>(dtheta), sin<Real>(dtheta));

    Matrix<Real> Mt(KDIM1*ChebOrder*FourierOrder, KDIM0*ChebOrder*FourierModes*2);
    for (Long i = 0; i < ChebOrder; i++) {
      Real r_trg = radius[i];
      Real s_trg = CenterlineNodes(ChebOrder)[i];
      Vec3 x_trg, dx_trg, e1_trg, e2_trg;
      { // Set x_trg, e1_trg, e2_trg
        for (Integer k = 0; k < COORD_DIM; k++) {
          x_trg (k,0) = coord[k*ChebOrder+i];
          e1_trg(k,0) = e1[k*ChebOrder+i];
          dx_trg(k,0) = dx[k*ChebOrder+i];
        }
        Real inv_dx2 = 1/dot_prod(dx_trg,dx_trg);
        e1_trg = e1_trg - dx_trg * dot_prod(e1_trg, dx_trg) * inv_dx2;
        e1_trg = e1_trg * (1/sqrt<Real>(dot_prod(e1_trg,e1_trg)));

        e2_trg = cross_prod(e1_trg, dx_trg);
        e2_trg = e2_trg * (1/sqrt<Real>(dot_prod(e2_trg,e2_trg)));
      }

      Vector<Real> quad_nds, quad_wts; // Quadrature rule in s
      SpecialQuadRule(quad_nds, quad_wts, ChebOrder, s_trg, r_trg, sqrt<Real>(dot_prod(dx_trg, dx_trg)), digits);

      Matrix<Real> Minterp_quad_nds;
      { // Set Minterp_quad_nds
        Minterp_quad_nds.ReInit(ChebOrder, quad_nds.Dim());
        Vector<Real> Vinterp_quad_nds(ChebOrder*quad_nds.Dim(), Minterp_quad_nds.begin(), false);
        LagrangeInterp<Real>::Interpolate(Vinterp_quad_nds, CenterlineNodes(ChebOrder), quad_nds);
      }

      Matrix<Real> r_src, dr_src, x_src, dx_src, d2x_src, e1_src, e2_src, de1_src, de2_src;
      r_src  .ReInit(        1,quad_nds.Dim());
      dr_src .ReInit(        1,quad_nds.Dim());
      x_src  .ReInit(COORD_DIM,quad_nds.Dim());
      dx_src .ReInit(COORD_DIM,quad_nds.Dim());
      d2x_src.ReInit(COORD_DIM,quad_nds.Dim());
      e1_src .ReInit(COORD_DIM,quad_nds.Dim());
      e2_src .ReInit(COORD_DIM,quad_nds.Dim());
      de1_src.ReInit(COORD_DIM,quad_nds.Dim());
      de2_src.ReInit(COORD_DIM,quad_nds.Dim());
      { // Set x_src, x_trg (improve numerical stability)
        Matrix<Real> x_nodes(COORD_DIM,ChebOrder, (Iterator<Real>)coord.begin(), true);
        for (Long j = 0; j < ChebOrder; j++) {
          for (Integer k = 0; k < COORD_DIM; k++) {
            x_nodes[k][j] -= x_trg(k,0);
          }
        }
        Matrix<Real>::GEMM(  x_src, x_nodes, Minterp_quad_nds);
        for (Integer k = 0; k < COORD_DIM; k++) {
          x_trg(k,0) = 0;
        }
      }
      //Matrix<Real>::GEMM(  x_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>) coord.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM( dx_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)    dx.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM(d2x_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)   d2x.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM(  r_src, Matrix<Real>(        1,ChebOrder,(Iterator<Real>)radius.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM( dr_src, Matrix<Real>(        1,ChebOrder,(Iterator<Real>)    dr.begin(),false), Minterp_quad_nds);
      Matrix<Real>::GEMM( e1_src, Matrix<Real>(COORD_DIM,ChebOrder,(Iterator<Real>)    e1.begin(),false), Minterp_quad_nds);
      for (Long j = 0; j < quad_nds.Dim(); j++) { // Set e2_src
        Vec3 e1, dx, d2x;
        for (Integer k = 0; k < COORD_DIM; k++) {
          e1(k,0) = e1_src[k][j];
          dx(k,0) = dx_src[k][j];
          d2x(k,0) = d2x_src[k][j];
        }
        Real inv_dx2 = 1/dot_prod(dx,dx);
        e1 = e1 - dx * dot_prod(e1, dx) * inv_dx2;
        e1 = e1 * (1/sqrt<Real>(dot_prod(e1,e1)));

        Vec3 e2 = cross_prod(e1, dx);
        e2 = e2 * (1/sqrt<Real>(dot_prod(e2,e2)));
        Vec3 de1 = dx*(-dot_prod(e1,d2x) * inv_dx2);
        Vec3 de2 = dx*(-dot_prod(e2,d2x) * inv_dx2);
        for (Integer k = 0; k < COORD_DIM; k++) {
          e1_src[k][j] = e1(k,0);
          e2_src[k][j] = e2(k,0);
          de1_src[k][j] = de1(k,0);
          de2_src[k][j] = de2(k,0);
        }
      }

      Complex<Real> exp_theta_trg(1,0);
      for (Long j = 0; j < FourierOrder; j++) {
        const Vec3 y_trg = x_trg + e1_trg*r_trg*exp_theta_trg.real + e2_trg*r_trg*exp_theta_trg.imag;

        Matrix<Real> M_tor(quad_nds.Dim(), FourierModes*2*KDIM0*KDIM1); // TODO: pre-allocate
        toroidal_greens_fn_batched(M_tor, y_trg, x_src, dx_src, d2x_src, r_src, dr_src, e1_src, e2_src, de1_src, de2_src, ker, FourierModes, digits);

        for (Long ii = 0; ii < M_tor.Dim(0); ii++) {
          for (Long jj = 0; jj < FourierModes*2*KDIM0*KDIM1; jj++) {
            M_tor[ii][jj] *= quad_wts[ii];
          }
        }
        Matrix<Real> M_(ChebOrder, FourierModes*2*KDIM0*KDIM1); // TODO: pre-allocate
        Matrix<Real>::GEMM(M_, Minterp_quad_nds, M_tor);

        for (Long ii = 0; ii < ChebOrder*FourierModes*2; ii++) { // Mt <-- M_
          for (Long k0 = 0; k0 < KDIM0; k0++) {
            for (Long k1 = 0; k1 < KDIM1; k1++) {
              Mt[(k1*ChebOrder+i)*FourierOrder+j][k0*ChebOrder*FourierModes*2+ii] = M_[0][(ii*KDIM0+k0)*KDIM1+k1];
            }
          }
        }
        exp_theta_trg *= exp_dtheta;
      }
    }

    Matrix<Real> Mt_(KDIM1*ChebOrder*FourierOrder, KDIM0*ChebOrder*FourierOrder);
    { // Set Mt_
      const Matrix<Real> M_modal(KDIM1*ChebOrder*FourierOrder*KDIM0*ChebOrder, FourierModes*2, Mt.begin(), false);
      Matrix<Real> M_nodal(KDIM1*ChebOrder*FourierOrder*KDIM0*ChebOrder, FourierOrder, Mt_.begin(), false);
      Matrix<Real>::GEMM(M_nodal, M_modal, M_fourier_inv);
    }

    Matrix<Real> M(ChebOrder*FourierOrder*KDIM0, ChebOrder*FourierOrder*KDIM1);
    { // Set M
      const Integer Nnds = ChebOrder*FourierOrder;
      for (Integer i0 = 0; i0 < Nnds; i0++) {
        for (Integer i1 = 0; i1 < KDIM0; i1++) {
          for (Integer j0 = 0; j0 < Nnds; j0++) {
            for (Integer j1 = 0; j1 < KDIM1; j1++) {
              M[i0*KDIM0+i1][j0*KDIM1+j1] = Mt_[j1*Nnds+j0][i1*Nnds+i0];
            }
          }
        }
      }
    }
    return M;
  }

}
