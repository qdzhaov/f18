! Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
!
! Licensed under the Apache License, Version 2.0 (the "License");
! you may not use this file except in compliance with the License.
! You may obtain a copy of the License at
!
!     http://www.apache.org/licenses/LICENSE-2.0
!
! Unless required by applicable law or agreed to in writing, software
! distributed under the License is distributed on an "AS IS" BASIS,
! WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
! See the License for the specific language governing permissions and
! limitations under the License.

module m
  integer(8), parameter :: a = 1, b = 2_8
  parameter(n=3)
  real :: x(a:2*(a+b*n)-1)
  real, dimension(8) :: y
  type t(c, d)
    integer, kind :: c = 1
    integer, len :: d = a + b
  end type
  type(t(3,:)), allocatable :: z
contains
  subroutine foo(x)
    real :: x(2:)
  end
  subroutine bar(x)
    real :: x(..)
  end
end

!Expect: m.mod
!module m
!  integer(8),parameter::a=1_4
!  integer(8),parameter::b=2_8
!  integer(4),parameter::n=3_4
!  real(4)::x(1_4:13_8)
!  real(4)::y(1_8:8_4)
!  type::t(c,d)
!    integer(4),kind::c=1_4
!    integer(4),len::d=3_8
!  end type
!  type(t(3_4,:)),allocatable::z
!contains
!  subroutine foo(x)
!    real(4)::x(2_4:)
!  end
!  subroutine bar(x)
!    real(4)::x(..)
!  end
!end
