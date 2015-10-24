Name:       app-core
Summary:    Application basic
Version:    1.3.40
Release:    1
Group:      TO_BE/FILLED_IN
License:    Apache-2.0
Source0:    app-core-%{version}.tar.gz
Source101:  packaging/core-efl.target
BuildRequires:  pkgconfig(sensor)
BuildRequires:  pkgconfig(vconf)
BuildRequires:  pkgconfig(aul)
BuildRequires:  pkgconfig(rua)
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xcomposite)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(elementary)
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(ecore-x)
BuildRequires:  pkgconfig(ecore-evas)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(edje)
BuildRequires:  pkgconfig(eet)
BuildRequires:  pkgconfig(eina)
BuildRequires:  pkgconfig(gobject-2.0)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(pkgmgr-info)
BuildRequires:  pkgconfig(ttrace)
BuildRequires:  pkgconfig(iniparser)
BuildRequires:  pkgconfig(dbus-glib-1)
BuildRequires:  cmake

%description
SLP common application basic

%define appfw_feature_process_pool 1
%define appfw_feature_capture_snapshot 0

%package efl
Summary:    App basic EFL
Group:      Development/Libraries
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description efl
Application basic EFL

%package efl-devel
Summary:    App basic EFL (devel)
Group:      Development/Libraries
Requires:   %{name}-efl = %{version}-%{release}
Requires:   %{name}-common-devel = %{version}-%{release}

%description efl-devel
Application basic EFL (devel)

%package common
Summary:    App basics common
Group:      Development/Libraries
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description common
Application basics common

%package common-devel
Summary:    App basics common (devel)
Group:      Development/Libraries
Requires:   %{name}-common = %{version}-%{release}
Requires:   pkgconfig(sensor)
Requires:   pkgconfig(vconf)
Requires:   pkgconfig(elementary)
Requires:   pkgconfig(aul)
Requires:   pkgconfig(x11)

%description common-devel
Application basics common (devel)

%package template
Summary:    App basics template
Group:      Development/Libraries

%description template
Application basics template

%if "%{?tizen_profile_name}" == "wearable"
%define appfw_feature_visibility_check_by_lcd_status 1
%define appfw_feature_sensor_auto_rotation 1
%define appfw_feature_background_management 1
%else
%if "%{?tizen_profile_name}" == "mobile"
%define appfw_feature_visibility_check_by_lcd_status 1
%define appfw_feature_sensor_auto_rotation 1
%define appfw_feature_background_management 1
%else
%if "%{?tizen_profile_name}" == "tv"
%define appfw_feature_visibility_check_by_lcd_status 0
%define appfw_feature_sensor_auto_rotation 0
%define appfw_feature_background_management 0
%endif
%endif
%endif

%define appfw_feature_expansion_pkg_install 1

%prep
%setup -q

%build
export CFLAGS="$CFLAGS -DTIZEN_DEBUG_ENABLE"
export CXXFLAGS="$CXXFLAGS -DTIZEN_DEBUG_ENABLE"
export FFLAGS="$FFLAGS -DTIZEN_DEBUG_ENABLE"
%if 0%{?appfw_feature_visibility_check_by_lcd_status}
_APPFW_FEATURE_VISIBILITY_CHECK_BY_LCD_STATUS=ON
%endif
%if 0%{?appfw_feature_process_pool}
_APPFW_FEATURE_PROCESS_POOL=ON
%endif
%if 0%{?appfw_feature_capture_snapshot}
_APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER=ON
%endif
%if 0%{?appfw_feature_expansion_pkg_install}
_APPFW_FEATURE_EXPANSION_PKG_INSTALL=ON
%endif
%if 0%{?appfw_feature_sensor_auto_rotation}
_APPFW_FEATURE_SENSOR_AUTO_ROTATION=ON
%endif
%if 0%{?appfw_feature_background_management}
_APPFW_FEATURE_BACKGROUND_MANAGEMENT=ON
%endif

#export CFLAGS="$CFLAGS -Wall -Werror -Wno-unused-function"
cmake -DCMAKE_INSTALL_PREFIX=%{_prefix} -DENABLE_GTK=OFF \
	-D_APPFW_FEATURE_PROCESS_POOL:BOOL=${_APPFW_FEATURE_PROCESS_POOL} \
	-D_APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER:BOOL=${_APPFW_FEATURE_CAPTURE_FOR_TASK_MANAGER} \
	-D_APPFW_FEATURE_EXPANSION_PKG_INSTALL:BOOL=${_APPFW_FEATURE_EXPANSION_PKG_INSTALL} \
	-D_APPFW_FEATURE_VISIBILITY_CHECK_BY_LCD_STATUS:BOOL=${_APPFW_FEATURE_VISIBILITY_CHECK_BY_LCD_STATUS} \
	-D_APPFW_FEATURE_SENSOR_AUTO_ROTATION:BOOL=${_APPFW_FEATURE_SENSOR_AUTO_ROTATION} \
	-D_APPFW_FEATURE_BACKGROUND_MANAGEMENT:BOOL=${_APPFW_FEATURE_BACKGROUND_MANAGEMENT} \
	.

%if "%{?tizen_profile_name}" == "wearable"
export CFLAGS="$CFLAGS -DWEARABLE"
%else
%if "%{?tizen_profile_name}" == "mobile"
export CFLAGS="$CFLAGS -DMOBILE"
%endif
%endif

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install
install -d %{buildroot}%{_libdir}/systemd/user/core-efl.target.wants
install -m0644 %{SOURCE101} %{buildroot}%{_libdir}/systemd/user/
mkdir -p %{buildroot}/usr/share/license
cp LICENSE %{buildroot}/usr/share/license/%{name}-efl
cp LICENSE %{buildroot}/usr/share/license/%{name}-common


%post efl

/sbin/ldconfig
mkdir -p /opt/usr/share/app_capture
chmod 777 /opt/usr/share/app_capture
chsmack -a "system::homedir" /opt/usr/share/app_capture
chsmack -t /opt/usr/share/app_capture

%postun efl -p /sbin/ldconfig

%post common -p /sbin/ldconfig

%postun common -p /sbin/ldconfig





%files efl
%manifest app-core.manifest
%defattr(-,root,root,-)
%{_libdir}/libappcore-efl.so.*
/usr/share/license/%{name}-efl

%files efl-devel
%defattr(-,root,root,-)
%{_includedir}/appcore/appcore-efl.h
%{_libdir}/libappcore-efl.so
%{_libdir}/pkgconfig/appcore-efl.pc

%files common
%manifest app-core.manifest
%defattr(-,root,root,-)
%{_libdir}/libappcore-common.so.*
%{_libdir}/systemd/user/core-efl.target
%{_libdir}/systemd/user/core-efl.target.wants/
/usr/share/license/%{name}-common

%files common-devel
%defattr(-,root,root,-)
%{_libdir}/libappcore-common.so
%{_libdir}/pkgconfig/appcore-common.pc
%{_includedir}/appcore/appcore-common.h
%{_includedir}/SLP_Appcore_PG.h

