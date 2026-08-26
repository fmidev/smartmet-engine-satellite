%define DIRNAME satellite
%define LIBNAME smartmet-%{DIRNAME}
%define SPECNAME smartmet-engine-%{DIRNAME}
Summary: SmartMet satellite engine
Name: %{SPECNAME}
Version: 26.8.26
Release: 5%{?dist}.fmi
License: MIT
Group: SmartMet/Engines
URL: https://github.com/fmidev/smartmet-engine-satellite
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

# https://fedoraproject.org/wiki/Changes/Broken_RPATH_will_fail_rpmbuild
%global __brp_check_rpaths %{nil}

%if 0%{?rhel} && 0%{rhel} < 9
%define smartmet_boost boost169
%else
%define smartmet_boost boost
%endif

%define smartmet_fmt_min 12.0.0
%define smartmet_fmt_max 13.0.0
%define smartmet_fmt fmt-libs >= %{smartmet_fmt_min}, fmt-libs < %{smartmet_fmt_max}
%define smartmet_fmt_devel fmt-devel >= %{smartmet_fmt_min}, fmt-devel < %{smartmet_fmt_max}

BuildRequires: rpm-build
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: %{smartmet_boost}-devel
BuildRequires: smartmet-library-spine-devel >= 26.8.24
BuildRequires: smartmet-library-macgyver-devel >= 26.8.24
BuildRequires: smartmet-utils-devel >= 26.8.24
BuildRequires: libconfig17-devel
BuildRequires: %{smartmet_fmt_devel}
BuildRequires: gdal312-devel
#TestRequires: smartmet-test-data >= 26.8.26
#TestRequires: smartmet-library-regression
Requires: %{smartmet_boost}-thread
Requires: %{smartmet_boost}-regex
Requires: smartmet-library-spine >= 26.8.24
Requires: smartmet-library-macgyver >= 26.8.24
Requires: libconfig17
Requires: %{smartmet_fmt}
Requires: gdal312-libs
Provides: %{SPECNAME}

%description
FMI SmartMet satellite engine serving precoloured satellite imagery
stored as cloud optimized GeoTIFF files.

%package -n %{SPECNAME}-devel
Summary: SmartMet %{SPECNAME} development headers
Group: SmartMet/Development
Provides: %{SPECNAME}-devel
Requires: %{SPECNAME} = %{version}-%{release}
Requires: smartmet-library-spine-devel >= 26.8.24
Requires: smartmet-library-macgyver-devel >= 26.8.24
%description -n %{SPECNAME}-devel
SmartMet %{SPECNAME} development headers.

%prep
rm -rf $RPM_BUILD_ROOT

%setup -q -n %{SPECNAME}

%build -q -n %{SPECNAME}
make %{_smp_mflags}

%install
%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%files -n %{SPECNAME}
%defattr(0775,root,root,0775)
%{_datadir}/smartmet/engines/%{DIRNAME}.so

%files -n %{SPECNAME}-devel
%defattr(0664,root,root,0775)
%{_includedir}/smartmet/engines/%{DIRNAME}/*.h

%changelog
* Wed Aug 26 2026 Mika Heiskanen <mika.heiskanen@fmi.fi> 26.8.26-5.fmi
- The warp result reports which overview level the pixels came from
* Wed Aug 26 2026 Mika Heiskanen <mika.heiskanen@fmi.fi> 26.8.26-4.fmi
- Tests use the images of the smartmet-test-data package
- Silenced the PROJ reports about points which are not on the Earth
* Wed Aug 26 2026 Mika Heiskanen <mika.heiskanen@fmi.fi> 26.8.26-3.fmi
- Added warpValues for products which hold values instead of colours
* Wed Aug 26 2026 Mika Heiskanen <mika.heiskanen@fmi.fi> 26.8.26-2.fmi
- Products are now identified by a producer and a parameter
* Wed Aug 26 2026 Mika Heiskanen <mika.heiskanen@fmi.fi> 26.8.26-1.fmi
- Initial release: satellite imagery engine for the WMS plugin
