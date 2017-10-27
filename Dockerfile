FROM base/archlinux

ADD .ssh/id_rsa.testbot /root/.ssh/id_rsa
ADD .ssh/id_dsa.testbot /root/.ssh/id_dsa
ADD .ssh/known_hosts    /root/.ssh/known_hosts

RUN chmod 600 /root/.ssh/id_rsa && \
    chmod 600 /root/.ssh/id_dsa

ENV PATH=$PATH:/gt/bin:/usr/synth/bin/ \
    GT_ROOT=/gt \
    LD_LIBRARY_PATH=/usr/synth/lib/ \
    USER=docker \
    HOSTNAME=docker

RUN pacman --noconfirm -Syu

RUN pacman --noconfirm -Syu archlinux-keyring

# Requirements to build clang-mutate.
RUN pacman --noconfirm -Syu base-devel findutils openssh git sed wget rsync gzip pandoc

# Requirements to test clang-mutate.
RUN pacman --noconfirm -Syu jshon libxcb

# Install specific version (4.0.1) of clang/llvm.
RUN mkdir -p /gt/pkgs && \
    cd /gt/pkgs && \
    export PKGS="clang-4.0.1-5-x86_64.pkg.tar.xz clang-tools-extra-4.0.1-5-x86_64.pkg.tar.xz llvm-4.0.1-5-x86_64.pkg.tar.xz llvm-libs-4.0.1-5-x86_64.pkg.tar.xz" && \
    echo $PKGS|tr ' ' '\n'|xargs -I{} wget http://otsego.grammatech.com/u1/eschulte/share-ro/pkgs/{} && \
    pacman --noconfirm -U $PKGS

# Don't upgrade clang/llvm packages.
RUN sed -i '/^#IgnorePkg/ a IgnorePkg = llvm llvm-libs clang clang-tools-extra' /etc/pacman.conf

# Enable makepkg as root.
RUN sed -i "s/^\(OPT_LONG=(\)/\1'asroot' /;s/EUID == 0/1 == 0/" /usr/bin/makepkg

# Install wdiff which is required by the clang tests.
RUN mkdir -p /gt/wdiff && \
    git clone https://aur.archlinux.org/wdiff.git /gt/wdiff && \
    cd /gt/wdiff && \
    makepkg --asroot --noconfirm -si

# Build clang-mutate package and install.
RUN mkdir -p /gt/clang-mutate/ && \
    git clone git@git.grammatech.com:synthesis/clang-mutate.git /gt/clang-mutate/ && \
    cd /gt/clang-mutate/ && \
    git checkout CI_COMMIT_SHA && \
    mkdir -p src/clang-mutate_pkg && \
    rsync --exclude .git --exclude src/clang-mutate_pkg -aruv ./ src/clang-mutate_pkg && \
    make -C src/clang-mutate_pkg clean && \
    makepkg --asroot --noconfirm -ef -si

WORKDIR /gt/clang-mutate

CMD /bin/bash
