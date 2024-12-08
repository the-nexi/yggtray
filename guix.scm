;; guix.scm --- GNU Guix package recipe    -*- coding: utf-8 -*-
;;
;; Copyright (C) 2024 Artyom V. Poptsov <poptsov.artyom@gmail.com>
;;
;; Author: Artyom V. Poptsov <poptsov.artyom@gmail.com>
;; Created: 8 December 2024
;;
;; This file is part of Yggtray.
;;
;; This program is free software: you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.
;;
;; The program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with the program.  If not, see <http://www.gnu.org/licenses/>.


;;; Commentary:
;;
;; GNU Guix development package.  To use as the basis for a development
;; environment, run:
;;
;;   guix shell -D -f guix.scm
;;
;; After that follow the build instructions from the README file.
;;
;; To build the package with GNU Guix, run:
;;
;;   guix build -f guix.scm
;;
;;; Code:


(use-modules (guix gexp)
             (guix packages)
             (guix licenses)
             (guix git-download)
             (guix build-system cmake)
             (gnu packages autotools)
             (gnu packages cmake)
             (gnu packages documentation)
             (gnu packages networking)
             (gnu packages qt))


(define %source-dir (dirname (current-filename)))


(define yggtray
  (package
    (name "yggtray")
    (version "git")
    (source (local-file %source-dir
                        #:recursive? #t
                        #:select? (git-predicate %source-dir)))
    (build-system cmake-build-system)
    (arguments
     (list
      #:tests? #f                       ; No tests.
      #:phases #~(modify-phases %standard-phases
                   (replace 'install
                     (lambda _
                       (let ((bin (string-append #$output "/bin/")))
                         (mkdir-p bin)
                         (invoke "ls" "-lha")
                         (copy-file "tray"
                                    (string-append bin "/yggtray"))))))))
    (native-inputs
     (list cmake
           doxygen))
    (inputs
     (list qtbase-5
           yggdrasil))
    (home-page "https://github.com/the-nexi/yggtray")
    (synopsis "Yggdrasil tray and control panel")
    (description
     "@code{yggdrasil} tray and control panel.  It allows to configure, run and
control the Yggdrasil daemon.")
    (license gpl3+)))

yggtray

;;; guix.scm ends here.
