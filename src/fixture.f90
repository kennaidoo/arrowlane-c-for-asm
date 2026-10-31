! The Fortran slice.
!
! Two jobs, both numeric: build the benchmark fixture as plain arrays, and compute
! the median the benchmark reports. The wire format is byte work, so it stays in C.
module aln_fixture_mod
  use, intrinsic :: iso_c_binding
  implicit none
  private
  public :: aln_fill_fixture, aln_median

  integer, parameter :: max_samples = 4096

contains

  ! The same three rows the Python reference builds, and the same values at any
  ! row count: id counts up, score is id/3, ok alternates, day starts at 20454,
  ! the decimal is 12.34, the timestamp is fixed, and the blob is 00 01 02.
  subroutine aln_fill_fixture(rows, id, name_is_null, score, ok, day, amount, ts_us, blob) &
      bind(c, name="aln_fill_fixture")
    integer(c_int64_t), value, intent(in) :: rows
    integer(c_int64_t), intent(out) :: id(*)
    integer(c_int8_t), intent(out) :: name_is_null(*)
    real(c_double), intent(out) :: score(*)
    integer(c_int8_t), intent(out) :: ok(*)
    integer(c_int32_t), intent(out) :: day(*)
    integer(c_int64_t), intent(out) :: amount(*)
    integer(c_int64_t), intent(out) :: ts_us(*)
    integer(c_int8_t), intent(out) :: blob(*)
    integer(c_int64_t) :: i, base

    do i = 1_c_int64_t, rows
      base = i - 1_c_int64_t
      id(i) = base
      ! The reference nulls every seventh row, not only the first.
      if (mod(base, 7_c_int64_t) == 0_c_int64_t) then
        name_is_null(i) = 1_c_int8_t
      else
        name_is_null(i) = 0_c_int8_t
      end if
      score(i) = real(base, c_double) / 3.0_c_double
      if (mod(base, 2_c_int64_t) == 0_c_int64_t) then
        ok(i) = 1_c_int8_t
      else
        ok(i) = 0_c_int8_t
      end if
      ! The reference wraps the day every 28 rows: 2026-01-01 to 2026-01-28.
      day(i) = 20454_c_int32_t + int(mod(base, 28_c_int64_t), c_int32_t)
      amount(i) = 1234_c_int64_t
      ts_us(i) = 1793188800000000_c_int64_t
      blob(3_c_int64_t * base + 1_c_int64_t) = 0_c_int8_t
      blob(3_c_int64_t * base + 2_c_int64_t) = 1_c_int8_t
      blob(3_c_int64_t * base + 3_c_int64_t) = 2_c_int8_t
    end do
  end subroutine aln_fill_fixture

  ! Median of a sample set, with a selection sort. The benchmark uses a few dozen
  ! samples, so the simple loop is the honest choice.
  function aln_median(values, count) result(median) bind(c, name="aln_median")
    integer(c_int64_t), value, intent(in) :: count
    real(c_double), intent(in) :: values(*)
    real(c_double) :: median
    real(c_double) :: work(max_samples)
    real(c_double) :: swap
    integer :: i, j, smallest, n

    if (count <= 0_c_int64_t) then
      median = 0.0_c_double
      return
    end if
    n = int(min(count, int(max_samples, c_int64_t)))
    do i = 1, n
      work(i) = values(i)
    end do
    do i = 1, n - 1
      smallest = i
      do j = i + 1, n
        if (work(j) < work(smallest)) then
          smallest = j
        end if
      end do
      if (smallest /= i) then
        swap = work(i)
        work(i) = work(smallest)
        work(smallest) = swap
      end if
    end do
    if (mod(n, 2) == 1) then
      median = work((n + 1) / 2)
    else
      median = 0.5_c_double * (work(n / 2) + work(n / 2 + 1))
    end if
  end function aln_median

end module aln_fixture_mod
